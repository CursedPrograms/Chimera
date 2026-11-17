// Chimera - filling holes and healing, after PhotoCraft (github.com/storytold/photocraft, crates/algo/src/inpaint.rs and
// poisson.rs, MIT OR Apache-2.0):
//  - complete(): content-aware fill, the multi-scale completion of Wexler, Shechtman & Irani (TPAMI 2007) with the
//    nearest-neighbour field from PatchMatch (Barnes et al., SIGGRAPH 2009): coarse to fine over a pyramid, the
//    coarsest level filled from the hole's edge inwards, then each EM iteration matches every patch over the hole to
//    its most similar source patch and re-estimates the hole by a similarity-weighted vote. Spot Healing, Remove,
//    Content-Aware Fill and Content-Aware Move use it.
//  - membrane(): Laplace's equation over a region from its edge (the membrane / seamless cloning of Perez et al. 2003
//    in its simplest form): the Healing Brush and the Patch tool copy texture and take the colour from round it.
#pragma once
#include <cstdint>
#include <vector>

namespace Inpaint {

// img: w x h x 3 floats (0..1), filled where hole is set, copying from where `source` is set (null: anywhere outside
// the hole). Returns false when there's nothing to copy from (img is then left as it was)
bool complete(int w, int h, std::vector<float>& img, const std::vector<uint8_t>& hole, const std::vector<uint8_t>* source = nullptr);

// v: w x h values; where unknown is set they're replaced by the smooth membrane that meets the known ones round them
void membrane(int w, int h, std::vector<float>& v, const std::vector<uint8_t>& unknown);

}
