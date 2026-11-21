# Modern C++20 작성규칙(필수로 읽기)

1. 스마트 포인터는 진짜 필요할때만 쓰고 가급적 매우 사용하지 않는다.
2. std::vector을 최대한 활용한다.
3. C언어에 쓰이는 malloc, free, gets_s, scanf같은 문법을 절떄 쓰지 않는다.
4. template, constexpr, inline, concept는 사용 가능하다.(단 과도하게는 사용 금지)
5. class를 쓸떄는 상속 기반 다형성, 과도한 객체지향 계층 구조, 불필요한 추상화 같은 과도한 OOP요소를 금지하며 오직 class는
의미를 명확히 하기위한 용도로만 사용이 가능하다. .cpp 파일에서는 새로운 class 및 struct의 정의를 원칙적으로 금지한다. 자료형의 정의는 .hpp에 작성하고 .cpp에서는 해당 자료형을 사용하거나 멤버 함수를 구현한다.

6. 하나의 class가 비대하게 커지는 God class를 절떄금지한다. 
7. RAII와 Move Semantics의 사용을 강력하게 허락한다.
8. operator는 의미가 진짜 초등학생도 봤을떄 명확할정도에만 사용이 가능하며 이 외에 경우에는 절때쓰지 않는다.
9. 프로젝트에서 직접 정의하는 struct는 데이터 보관 용도로만 사용하며, 멤버 함수는 정의하지 않는다. 기능 구현은 별도의 일반 함수 또는 class에서 담당한다.

예제코드(이걸 배껴쓰는게 아니라 이런 스타일로 작성하라는뜻. 여기서 있는 기술적문제까지 배끼라는건 절떄아님.)
```cpp basic_loop.cpp
#include "basic/basic_loop.hpp"
#include "system_3d/system3D.hpp"
#include "PCG_System/assetLoader/asset_loader.hpp"
#include "PCG_System/assetLoader/fbx_loader.hpp"

#include <raylib.h>
#include <iostream>

void MainLoop::mainDraw()
{
    DisableCursor();
    
    SetTargetFPS(60);

    // setting is here
    ModelLoaderAuto loaderAuto;

    /*
    loaderAuto.InitalizeAsset(
        "C:/Users/kym10/Documents/VSCodeProj/PCGEngine/src/asset_test/castle.obj",
        "C:/Users/kym10/Documents/VSCodeProj/PCGEngine/src/asset_test/castle_diffuse.png"
    );
    */

    FBXLoader fbx_loader(
        "C:/Users/kym10/Documents/VSCodeProj/PCGEngine/src/asset_test/fbx_test/Fanal_Laurisilva_Tree_dtre_Raw/Fanal_Laurisilva_Tree_dtre_Raw.fbx"
    );

    // Camera3D
    Camera3D camsetting = basic3d.Basic3DCamSetting();

    // Main game loop
    while (!WindowShouldClose())    // Detect window close button or ESC key
    {
        // Update
        //----------------------------------------------------------------------------------
        UpdateCamera(&camsetting, CAMERA_FREE);
        // TODO: Update your variables here
        //----------------------------------------------------------------------------------

        // Draw
        //----------------------------------------------------------------------------------
        BeginDrawing();

            ClearBackground(RAYWHITE);

            basic3d.Draw3DStartEnd(camsetting, loaderAuto, fbx_loader);

            // [FOR DEBUG] std::cout << camsetting.position.x << camsetting.position.y << camsetting.position.z;

        EndDrawing();
        //----------------------------------------------------------------------------------
    }

    // loaderAuto.DisalbeAsset();
}
```

```hpp basic_loop.hpp
#pragma once
#include "system_3d/system3D.hpp"

class MainLoop
{
public:
    void mainDraw();

private:
    Basic3D basic3d;
};
```

```hpp basic_window.hpp
#pragma once
#include <raylib.h>

class BasicWindow
{
public:

    BasicWindow(const int screenWidth, const int screenHeight)
    {
        InitWindow(screenWidth, screenHeight, "Basic PCG");
    }

    ~BasicWindow()
    {
        CloseWindow();
    }

    // 복사 금지
    BasicWindow(const BasicWindow&) = delete;
    BasicWindow& operator=(const BasicWindow&) = delete;

    // 이동 금지
    BasicWindow(BasicWindow&&) = delete;
    BasicWindow& operator=(BasicWindow&&) = delete;
};
```

```cpp system3D.cpp
#include "system_3d/system3D.hpp"
#include "./PCG_System/assetLoader/asset_loader.hpp"
#include "./PCG_System/assetLoader/fbx_loader.hpp"

#include <raylib.h>
#include <raymath.h>

void Basic3D::Draw3DStartEnd(Camera3D camera, ModelLoaderAuto& loaderAuto, FBXLoader& fbx_loader)
{
    Material defaultMaterial = LoadMaterialDefault();
    defaultMaterial.maps[MATERIAL_MAP_DIFFUSE].color = RED; // 붉은색 큐브

    Matrix rotation = MatrixRotateY((float)GetTime());
    Matrix translation = MatrixTranslate(0.0f, 0.0f, 0.0f);
    Matrix transform = MatrixMultiply(rotation, translation);

    BeginMode3D(camera);
    
        // loaderAuto.DrawAsset({0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
        DrawMesh(fbx_loader.CreateFBX(), defaultMaterial, transform);
        DrawGrid(1000, 10.0f);

    EndMode3D();
}

Camera3D Basic3D::Basic3DCamSetting()
{
    // Define the camera to look into our 3d world
    Camera3D camera = { 0 };
    camera.position = Vector3{ 10.0f, 10.0f, 10.0f }; // Camera position
    camera.target = Vector3{ 0.0f, 0.0f, 0.0f };      // Camera looking at point
    camera.up = Vector3{ 0.0f, 1.0f, 0.0f };          // Camera up vector (rotation towards target)
    camera.fovy = 45.0f;                                // Camera field-of-view Y
    camera.projection = CAMERA_PERSPECTIVE;             // Camera projection type

    return camera;
}
```

```hpp system3D.hpp
#pragma once

struct Camera3D;

class ModelLoaderAuto;
class FBXLoader;

// 기초적 3D 동작을 하는 클래스.
class Basic3D
{
public:
    // 이 함수는 기초3D화면에 관한 세팅을 하는 함수입니다. 
    Camera3D Basic3DCamSetting();

    // 이 함수는 mainLoop에서 3D를 그려주는걸 선언해주는 함수 입니다.
    void Draw3DStartEnd(Camera3D camera, ModelLoaderAuto& loaderAuto, FBXLoader& fbx_loader);
};
```