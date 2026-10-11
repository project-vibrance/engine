#pragma once

#include "../common/text_batch.h"

namespace vibrance::vulkan
{
    using vibrance::renderer::kTextBatchGlyphLimit;
    using vibrance::renderer::kTextBatchPlanningGlyphLimit;
    using vibrance::renderer::TextBatchGroup;
    using vibrance::renderer::TextBatchPlan;
    using vibrance::renderer::text_batch_bounds_overlap;
    using vibrance::renderer::build_text_batch_plan;
}
