# Sourced by unixBuild.sh before argument validation. Keep one complete log per
# invocation, including dependency builds, and preserve the build's exit status.
vibrance_logged_build() {
  local component=$1 script=$2
  shift 2
  local guard="VIBRANCE_BUILD_LOG_${component}"
  if [[ ${!guard:-} == 1 ]]; then return; fi
  local root=${VIBRANCE_COMPILATION_LOG_ROOT:-"${HOME}/Documents/vibrance-dev/compilations"}
  local outcome project
  for outcome in successes failures; do
    for project in app engine installer; do mkdir -p "${root}/${outcome}/${project}"; done
  done
  local run="$(date -u +%Y-%m-%d_%H-%M-%S)-$$-${RANDOM}"
  local log="${root}/failures/${component}/${run}.log"
  local status=1
  printf 'Component: %s; started (UTC): %s\n' "${component}" "$(date -u)" > "${log}"
  echo "Compilation log: ${log}"
  # Disable errexit only around the pipeline to retain PIPESTATUS on failure.
  set +e
  env "${guard}=1" bash "${script}" "$@" 2>&1 | tee -a "${log}"
  local pipeline_status=("${PIPESTATUS[@]}")
  status=${pipeline_status[0]}
  if (( status == 0 && pipeline_status[1] != 0 )); then status=${pipeline_status[1]}; fi
  set -e
  if (( status == 0 )); then
    printf '\n================ COMPILATION SUCCEEDED ================\n' >> "${log}"
    local destination="${root}/successes/${component}/${run}.log"
    mv "${log}" "${destination}"
    echo "COMPILATION SUCCEEDED. Full log: ${destination}"
  else
    local summary="${root}/failures/${component}/${run}.failure.txt"
    {
      echo '!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!'
      echo "COMPILATION FAILED: ${component}; exit code ${status}"
      echo 'FAILURE REASON / COMPILER DIAGNOSTICS:'
      grep -i -m 12 -A 5 -E 'fatal error|error:|CMake Error|undefined reference|FAILED:|not found|missing|cannot|could not' "${log}" || tail -n 30 "${log}"
      echo "FULL COMPILATION LOG: ${log}"
      echo '!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!'
    } > "${summary}"
    cat "${summary}" | tee -a "${log}" >&2
  fi
  exit "${status}"
}
