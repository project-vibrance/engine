#include "../src/vibranceUI/animation/lottie.h"

#include <cassert>
#include <string_view>

int main()
{
    constexpr std::string_view trimmedLine = R"json(
{
  "v":"5.12.1","fr":60,"ip":0,"op":60,"w":100,"h":100,
  "layers":[{
    "ind":1,"ty":4,"ip":0,"op":60,"ks":{},
    "shapes":[{"ty":"gr","it":[
      {"ty":"sh","ks":{"a":0,"k":{"i":[[0,0],[0,0]],"o":[[0,0],[0,0]],"v":[[10,50],[90,50]],"c":false}}},
      {"ty":"tm","s":{"a":1,"k":[{"t":0,"s":[50],"i":{"x":[0.666667],"y":[0.666667]},"o":{"x":[0.333333],"y":[0.333333]}},{"t":60,"s":[0]}]},"e":{"a":1,"k":[{"t":0,"s":[50],"i":{"x":[0.666667],"y":[0.666667]},"o":{"x":[0.333333],"y":[0.333333]}},{"t":60,"s":[100]}]},"o":{"a":0,"k":0},"m":1},
      {"ty":"st","w":{"a":0,"k":4},"lc":2},
      {"ty":"tr"}
    ]}]
  }]
}
)json";

    const vibrance::animation::LottieSvgAnimation animation =
        vibrance::animation::make_lottie_svg_animation(trimmedLine, 3u);

    assert(animation.valid());
    assert(animation.frames.size() == 3u);
    assert(animation.unsupportedFeatureCount == 0u);
    assert(animation.frames[0].svg != animation.frames[1].svg);
    assert(animation.frames[1].svg != animation.frames[2].svg);
    assert(animation.frames[0].svg.find("stroke-opacity=\"0.00000\"") !=
        std::string::npos);
    assert(animation.frames[1].svg.find("stroke-dasharray=\"") ==
        std::string::npos);
    assert(animation.frames[1].svg.find("d=\"M36.") !=
        std::string::npos);
    assert(animation.frames[1].svg.find("63.3") !=
        std::string::npos);
    assert(animation.frames[2].svg.find("stroke-dasharray=\"") ==
        std::string::npos);

    return 0;
}
