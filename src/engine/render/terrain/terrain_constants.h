#pragma once

namespace types {
    enum
    {
        PROGRAM_TERRAIN_NORMAL,
        PROGRAM_TERRAIN,

        SHADING_COUNT
    };

    enum
    {
        BUFFER_SUBD
    };

    enum
    {
        PROGRAM_SUBD_CS_LOD,
        PROGRAM_UPDATE_INDIRECT,
        PROGRAM_INIT_INDIRECT,
        PROGRAM_UPDATE_DRAW,
        PROGRAM_GENERATE_SMAP,
        PROGRAM_RECT_MAX,

        PROGRAM_COUNT
    };

    enum
    {
        TERRAIN_DMAP_SAMPLER,
        TERRAIN_SMAP_SAMPLER,
        TERRAIN_DIFFUSE_SAMPLER,

        SAMPLER_COUNT
    };

    enum
    {
        TEXTURE_DMAP,
        TEXTURE_SMAP,
        TEXTURE_DIFFUSE,

        TEXTURE_COUNT
    };
}
