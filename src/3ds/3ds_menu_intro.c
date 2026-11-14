#include "3ds_menu_intro.h"
#include "../log.h"
#include <math.h>

#define INTRO_ROOT "romfs:/launcher/"
#define INTRO_DURATION 17.684208f

static float introFade(float time, float start, float duration) {
    return fminf(1, fmaxf(0, (time - start) / duration));
}

static void introDraw(C2D_Image image, float x, float y, float scale, float alpha) {
    if (alpha <= 0) return;
    C2D_ImageTint tint;
    C2D_AlphaImageTint(&tint, alpha);
    C2D_DrawImageAt(image, x, y, 0.1f, &tint, scale, scale);
}

enum { INTRO_LOGO, INTRO_HEART, INTRO_DEPTH, INTRO_NOISE, INTRO_IMAGES };

static void introTile(C2D_Image image, float scroll, uint32_t color, float alpha) {
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, (color & 0xffffff) | ((uint32_t)C2D_FloatToU8(alpha) << 24), 1);
    float startX = 40 - fmodf(scroll, image.subtex->width);
    while (startX > 0) startX -= image.subtex->width;
    float startY = -fmodf(scroll, image.subtex->height);
    for (float y = startY; y < 240; y += image.subtex->height)
        for (float x = startX; x < 400; x += image.subtex->width)
            C2D_DrawImageAt(image, x, y, 0.1f, &tint, 1, 1);
}

static void introRenderEffect(C3D_RenderTarget* target, C2D_Image* images,
    float tick, float speed, float time) {
    C2D_TargetClear(target, C2D_Color32(255, 255, 255, 0));
    C2D_SceneBegin(target);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_RED | GPU_WRITE_GREEN | GPU_WRITE_BLUE);
    C2D_SetTintMode(C2D_TintMult);
    uint32_t color = C2D_Color32(66, 208, 255, 255);
    introTile(images[INTRO_DEPTH], tick * 3.75f, color, 1);
    C2D_Flush();
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE,
        GPU_ONE, GPU_ONE);
    introTile(images[INTRO_NOISE], tick * 3.75f, color,
        0.2f + sinf(time * 6.2831853f / 4) * 0.2f);
    C2D_Flush();
    C2D_SetTintMode(C2D_TintSolid);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
        GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALPHA);
    C2D_Image logo = images[INTRO_LOGO];
    float x = roundf(200 - logo.subtex->width / 2.0f);
    float y = roundf(120 - logo.subtex->height / 2.0f);
    float offset = sinf(tick / 15 * 6.2831853f) * speed * 3;
    // just share one texture field
    introDraw(logo, x - offset, y - offset, 1, 0.4f);
    introDraw(logo, x + offset, y + offset, 1, 0.4f);
    introDraw(logo, x, y, 1, 1);
    C2D_Flush();
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_FrameSplit(0);
    C2D_Prepare();
}

static void introDrawLogo(C2D_Image* images, C2D_Image effect, float time) {
    C2D_Image logo = images[INTRO_LOGO], heart = images[INTRO_HEART];
    float x = roundf((400 - logo.subtex->width) / 2.0f);
    float y = roundf((240 - logo.subtex->height) / 2.0f);
    introDraw(logo, x, y, 1, introFade(time, 4, 3));
    if (time >= 7.5f) introDraw(effect, 0, 0, 1, introFade(time, 7.5f, 70.0f / 30));
    introDraw(heart, x, y, 1, introFade(time, 0, 4));
}

static void introClearScreens(C3D_RenderTarget* top, C3D_RenderTarget* bottom) {
    C2D_TargetClear(bottom, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(bottom);
    C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(top);
}

static bool introLoadBlack(C3D_RenderTarget* top, C3D_RenderTarget* bottom,
    bool (*loadNext)(void*), void* context) {
    bool loaded = false;
    while (!loaded) {
        if (!aptMainLoop()) return false;
        if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) return false;
        C2D_Prepare();
        introClearScreens(top, bottom);
        C3D_FrameEnd(0);
        loaded = loadNext(context);
    }
    return true;
}

bool n3ds_menuIntro(C3D_RenderTarget* top, C3D_RenderTarget* bottom,
    AudioSystem* audio, int volume, bool enabled, bool (*loadNext)(void*), void* context) {
    static bool shown = false;
    bool playIntro = !shown && enabled;
    shown = true;
    if (!playIntro) return introLoadBlack(top, bottom, loadNext, context);
    const char* paths[INTRO_IMAGES] = {INTRO_ROOT "intro_logo.t3x", INTRO_ROOT "intro_heart.t3x",
        INTRO_ROOT "intro_depth.t3x", INTRO_ROOT "intro_noise.t3x"};
    C2D_SpriteSheet sheets[INTRO_IMAGES] = {0};
    C2D_Image images[INTRO_IMAGES] = {0};
    C3D_Tex effectTexture = {0};
    bool textureLoaded = false;
    C3D_RenderTarget* effectTarget = NULL;
    bool active = true;
    bool loaded = false;
    int32_t music = -1, instance = -1;
    for (int i = 0; i < INTRO_IMAGES; ++i) {
        sheets[i] = C2D_SpriteSheetLoad(paths[i]);
        if (sheets[i] == NULL) {
            logWarn("Missing intro sprite: %s\n", paths[i]);
            goto cleanup;
        }
        images[i] = C2D_SpriteSheetGetImage(sheets[i], 0);
        C3D_TexSetFilter(images[i].tex, GPU_LINEAR, GPU_LINEAR);
    }
    textureLoaded = C3D_TexInitVRAM(&effectTexture, 512, 256, GPU_RGBA8);
    if (!textureLoaded) {
        logWarn("Could not allocate intro effect texture in VRAM\n");
        goto cleanup;
    }
    C3D_TexSetFilter(&effectTexture, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&effectTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    effectTarget = C3D_RenderTargetCreateFromTex(&effectTexture, GPU_TEXFACE_2D, 0, -1);
    if (effectTarget == NULL) {
        logWarn("Could not create intro effect render target\n");
        goto cleanup;
    }
    Tex3DS_SubTexture effectSubtex = {.width = 400, .height = 240,
        .left = 0, .right = 400.0f / 512, .top = 1, .bottom = 1 - 240.0f / 256};
    C2D_Image effectImage = {&effectTexture, &effectSubtex};
    if (volume > 0) {
        music = audio->vtable->createStream(audio, INTRO_ROOT "intro.ogg");
        if (music >= 0) {
            audio->vtable->setSoundGain(audio, music, volume / 100.0f, 0);
            instance = audio->vtable->playSound(audio, music, 0, false);
            if (instance >= 0) audio->vtable->setSoundGain(audio, instance, volume / 100.0f, 0);
        }
        if (music < 0 || instance < 0) logWarn("Could not play launcher intro music\n");
    }
    float elapsed = 0;
    float textureTick = 0, scrollSpeed = 2;
    int textureFrame = -1;
    bool skipped = false;
    u64 lastTick = svcGetSystemTick();
    hidScanInput();
    while (elapsed < INTRO_DURATION || !loaded) {
        if (!aptMainLoop()) {
            active = false;
            break;
        }
        u64 tick = svcGetSystemTick();
        float dt = fminf(0.1f, (float)((double)(tick - lastTick) / SYSCLOCK_ARM11));
        lastTick = tick;
        elapsed += dt;
        audio->vtable->update(audio, dt);
        if (instance >= 0 && audio->vtable->isPlaying(audio, instance)) {
            float position = audio->vtable->getTrackPosition(audio, instance);
            if (position > 0) elapsed = position;
        }
        hidScanInput();
        if (elapsed > 0.3f && (hidKeysDown() & (KEY_A | KEY_B | KEY_START | KEY_TOUCH))) {
            skipped = true;
            elapsed = INTRO_DURATION;
            if (instance >= 0) audio->vtable->stopSound(audio, instance);
            instance = -1;
        }
        if (!loaded) loaded = loadNext(context);
        if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) {
            active = false;
            break;
        }
        C2D_Prepare();
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
            GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
        C3D_AlphaTest(false, GPU_ALWAYS, 0);
        C3D_StencilTest(false, GPU_ALWAYS, 0, 0xFF, 0xFF);
        float drawTime = fminf(elapsed, INTRO_DURATION);
        int frame = (int)(drawTime * 30);
        while (textureFrame < frame) {
            ++textureFrame;
            scrollSpeed = 2 * (1 - introFade(textureFrame / 30.0f, 15, 2));
            textureTick += scrollSpeed / 15;
        }
        if (!skipped && drawTime >= 7.5f)
            introRenderEffect(effectTarget, images, textureTick, scrollSpeed, drawTime);
        introClearScreens(top, bottom);
        if (!skipped) introDrawLogo(images, effectImage, drawTime);
        C2D_Flush();
        C3D_FrameEnd(0);
        gspWaitForVBlank();
    }
    if (C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) C3D_FrameEnd(0);
cleanup:
    if (instance >= 0) audio->vtable->stopSound(audio, instance);
    if (music >= 0) audio->vtable->destroyStream(audio, music);
    if (effectTarget != NULL) C3D_RenderTargetDelete(effectTarget);
    if (textureLoaded) C3D_TexDelete(&effectTexture);
    for (int i = 0; i < INTRO_IMAGES; ++i)
        if (sheets[i] != NULL) C2D_SpriteSheetFree(sheets[i]);
    if (active && !loaded) return introLoadBlack(top, bottom, loadNext, context);
    return active;
}
