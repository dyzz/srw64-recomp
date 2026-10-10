"""Selectively keep original textures without unloading their streamed replacements.

The host changes the blocked hashes only after draining workloads and presents,
under textureMapMutex, and increments the affected texture versions. Both UV
calculation and descriptor snapshots must see the same selection.
"""

BY_FILE = {
    "src/render/rt64_texture_cache.h": [
        ("#include <unordered_map>", "#include <unordered_map>\n#include <unordered_set>"),
        ("        bool replacementMapEnabled;",
         "        bool replacementMapEnabled;\n"
         "        std::unordered_set<uint64_t> replacementBlockedHashes;\n"),
    ],
    "src/render/rt64_texture_cache.cpp": [
        ("        textureReplaced = replacementMapEnabled && (textureReplacements[textureIndex] != nullptr);",
         "        textureReplaced = replacementMapEnabled && (replacementBlockedHashes.count(hash) == 0) && (textureReplacements[textureIndex] != nullptr);"),
    ],
    "src/render/rt64_framebuffer_renderer.cpp": [
        ("        textureCacheTextureReplacements = textureCache->textureMap.textureReplacements;",
         "        textureCacheTextureReplacements = textureCache->textureMap.textureReplacements;\n"
         "        for (uint64_t hash : textureCache->textureMap.replacementBlockedHashes) {\n"
         "            const auto it = textureCache->textureMap.hashMap.find(hash);\n"
         "            if (it != textureCache->textureMap.hashMap.end()) {\n"
         "                textureCacheTextureReplacements[it->second] = nullptr;\n"
         "            }\n"
         "        }"),
    ],
}
