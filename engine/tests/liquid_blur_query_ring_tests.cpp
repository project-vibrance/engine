#include "../src/platform/win32/liquid_glass_d3d11.h"

#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

// The policy only owns/query-selects these COM objects. GPU correctness is
// covered separately by the native temporal-blur pixel comparison fixture.
class Predicate final : public ID3D11Predicate
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** value) override
    {
        if (value) *value = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override { return --references; }
    void STDMETHODCALLTYPE GetDevice(ID3D11Device** value) override { if (value) *value = nullptr; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override { return E_NOTIMPL; }
    UINT STDMETHODCALLTYPE GetDataSize() override { return sizeof(BOOL); }
    void STDMETHODCALLTYPE GetDesc(D3D11_QUERY_DESC* value) override
    {
        if (value) *value = { D3D11_QUERY_OCCLUSION_PREDICATE, 0 };
    }
    ULONG references = 0;
};
}

namespace vibrance::win32
{
struct LiquidGlassD3D11TestAccess
{
    static void run()
    {
        Predicate predicates[3];
        {
            LiquidGlassD3D11 renderer;
            for (std::size_t index = 0; index < 3; ++index)
                renderer.blurHistory_.comparisons[index].predicate = &predicates[index];

            for (std::size_t frame = 0; frame < 3; ++frame)
            {
                unsigned polled = 0;
                renderer.collect_blur_comparisons([&](ID3D11Predicate*, BOOL&) {
                    ++polled;
                    return S_FALSE;
                });
                require(polled == frame, "Pending queries were waited on or lost from the ring");
                auto* comparison = renderer.available_blur_comparison();
                require(comparison && comparison->predicate.Get() == &predicates[frame],
                    "A delayed CPU query prevented a fresh exact GPU comparison");
                comparison->pending = true;
            }
            require(renderer.available_blur_comparison() == nullptr,
                "A saturated pool reused a query whose result is still pending");

            renderer.collect_blur_comparisons([&](ID3D11Predicate* predicate, BOOL& changed) {
                changed = FALSE;
                return predicate == &predicates[0] ? S_OK : S_FALSE;
            });
            auto* recycled = renderer.available_blur_comparison();
            require(recycled && recycled->predicate.Get() == &predicates[0] &&
                renderer.blur_reuse_count() == 1,
                "A completed unchanged query was not recycled without waiting on the others");
            recycled->pending = true;

            renderer.collect_blur_comparisons([&](ID3D11Predicate* predicate, BOOL& changed) {
                changed = predicate == &predicates[0] ? FALSE : TRUE;
                return S_OK;
            });
            require(renderer.blur_change_count() == 2 && renderer.blur_reuse_count() == 2 &&
                renderer.blurHistory_.backoff == 32,
                "Completed ring results changed the exact-reuse or changing-background backoff policy");

            for (auto& comparison : renderer.blurHistory_.comparisons) comparison.pending = true;
            renderer.collect_blur_comparisons([&](ID3D11Predicate* predicate, BOOL&) {
                return predicate == &predicates[0] ? E_FAIL : S_FALSE;
            });
            require(!renderer.blurHistory_.comparisons[0].predicate &&
                !renderer.blurHistory_.comparisons[0].pending &&
                renderer.available_blur_comparison() == nullptr,
                "A failed query did not retire safely while the other slots were pending");
            renderer.collect_blur_comparisons([](ID3D11Predicate*, BOOL& changed) {
                changed = FALSE;
                return S_OK;
            });
            require(renderer.available_blur_comparison() != nullptr,
                "A retired query disabled healthy comparison slots");
        }
        for (const auto& predicate : predicates)
            require(predicate.references == 0, "A comparison predicate leaked its COM reference");
    }
};
}

int main()
{
    try
    {
        vibrance::win32::LiquidGlassD3D11TestAccess::run();
        std::cout << "Delayed blur-query ring policy passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
