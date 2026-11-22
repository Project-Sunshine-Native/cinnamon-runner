#pragma once

#include <citro2d.h>
#include <stdbool.h>

typedef struct {
    C3D_Tex texture;
    Tex3DS_SubTexture subtex;
    bool loaded;
} N3DSMenuIcon;

void n3ds_menuIconLoad(N3DSMenuIcon* icon, const char* folder);
void n3ds_menuIconFree(N3DSMenuIcon* icon);
