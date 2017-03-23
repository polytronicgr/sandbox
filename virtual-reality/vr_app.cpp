#include "vr_app.hpp"
#include "avl_imgui.hpp"
#include "asset_io.hpp"

// Material manager -> setup, get
// Serialization

VirtualRealityApp::VirtualRealityApp() : GLFWApp(1280, 800, "VR")
{
    scoped_timer t("constructor");

    int windowWidth, windowHeight;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);

    igm.reset(new gui::ImGuiManager(window));
    gui::make_dark_theme();

    gpuTimer.init();
    cameraController.set_camera(&debugCam);

    try
    {
        hmd.reset(new OpenVR_HMD());

        const uint2 targetSize = hmd->get_recommended_render_target_size();
        renderer.reset(new VR_Renderer({ (float)targetSize.x, (float)targetSize.y }));

        glfwSwapInterval(0);
    }
    catch (const std::exception & e)
    {
        std::cout << "OpenVR Exception: " << e.what() << std::endl;
        renderer.reset(new VR_Renderer({ (float)windowWidth * 0.5f, (float)windowHeight })); // per eye resolution
    }

    setup_physics();

    setup_scene();

    gl_check_error(__FILE__, __LINE__);
}

VirtualRealityApp::~VirtualRealityApp()
{
    hmd.reset();
}

void VirtualRealityApp::setup_physics()
{
    scoped_timer t("setup physics");

    physicsEngine.reset(new BulletEngineVR());

    physicsDebugRenderer.reset(new PhysicsDebugRenderer()); // Sets up a few gl objects
    physicsDebugRenderer->setDebugMode(
        btIDebugDraw::DBG_DrawWireframe |
        btIDebugDraw::DBG_DrawContactPoints |
        btIDebugDraw::DBG_DrawConstraints |
        btIDebugDraw::DBG_DrawConstraintLimits |
        btIDebugDraw::DBG_DrawFeaturesText | 
        btIDebugDraw::DBG_DrawText);

    // Hook up debug renderer
    physicsEngine->get_world()->setDebugDrawer(physicsDebugRenderer.get());
}

void VirtualRealityApp::setup_scene()
{
    AVL_SCOPED_TIMER("setup_scene");

    scene.directionalLight.direction = float3(0, -1.f, 0);
    scene.directionalLight.color = float3(1.f, 1.f, 1.f);
    scene.directionalLight.amount = 0.1f;

    scene.pointLights.push_back(uniforms::point_light{ float3(0.88f, 0.85f, 0.975f), float3(-1, 1, 0), 4.f });
    scene.pointLights.push_back(uniforms::point_light{ float3(0.67f, 1.f, 0.85f), float3(+1, 1, 0), 4.f });

    // Materials first since other objects need to reference them
    auto normalShader = shaderMonitor.watch("../assets/shaders/normal_debug_vert.glsl", "../assets/shaders/normal_debug_frag.glsl");
    scene.namedMaterialList["material-debug"] = std::make_shared<DebugMaterial>(normalShader);

    auto wireframeShader = shaderMonitor.watch("../assets/shaders/wireframe_vert.glsl", "../assets/shaders/wireframe_frag.glsl", "../assets/shaders/wireframe_geom.glsl");
    scene.namedMaterialList["material-wireframe"] = std::make_shared<WireframeMaterial>(wireframeShader);

    // Slightly offset from debug-rendered physics floor
    scene.grid.set_origin(float3(0, -.01f, 0));

    // Bullet considers an object with 0 mass infinite/static/immovable
    btCollisionShape * ground = new btStaticPlaneShape({ 0.f, +1.f, 0.f }, 0.f);
    auto groundObject = std::make_shared<BulletObjectVR>(new btDefaultMotionState(), ground, physicsEngine->get_world(), 0.0f);
    groundObject->body->setFriction(1.f);
    groundObject->body->setRestitution(0.9f); // very hard floor that absorbs energy
    physicsEngine->add_object(groundObject.get());
    scene.physicsObjects.push_back(groundObject);

    {
        StaticMesh cube;
        cube.set_static_mesh(make_cube(), 0.1f);
        cube.set_pose(Pose(float4(0, 0, 0, 1), float3(0, 2.0f, 0)));
        cube.set_material(scene.namedMaterialList["material-debug"].get());

        btCollisionShape * cubeCollisionShape = new btBoxShape(to_bt(cube.get_bounds().size() * 0.5f));
        auto cubePhysicsObj = std::make_shared<BulletObjectVR>(cube.get_pose().matrix(), cubeCollisionShape, physicsEngine->get_world(), 0.88f); // transformed
        cubePhysicsObj->body->setRestitution(0.4f);
        cube.set_physics_component(cubePhysicsObj.get());

        physicsEngine->add_object(cubePhysicsObj.get());
        scene.physicsObjects.push_back(cubePhysicsObj);
        scene.models.push_back(std::move(cube));
    }

    {
        auto radianceBinary = read_file_binary("../assets/textures/envmaps/wells_radiance.dds");
        auto irradianceBinary = read_file_binary("../assets/textures/envmaps/wells_irradiance.dds");

        gli::texture_cube radianceHandle(gli::load_dds((char *)radianceBinary.data(), radianceBinary.size()));
        gli::texture_cube irradianceHandle(gli::load_dds((char *)irradianceBinary.data(), irradianceBinary.size()));

        texDatabase.register_asset("wells-radiance-cubemap", load_cubemap(radianceHandle));
        texDatabase.register_asset("wells-irradiance-cubemap", load_cubemap(irradianceHandle));
        texDatabase.register_asset("rusted-iron-albedo", load_image("../assets/textures/pbr/rusted_iron_2048/albedo.png", true));
        texDatabase.register_asset("rusted-iron-normal", load_image("../assets/textures/pbr/rusted_iron_2048/normal.png", true));
        texDatabase.register_asset("rusted-iron-metallic", load_image("../assets/textures/pbr/rusted_iron_2048/metallic.png", true));
        texDatabase.register_asset("rusted-iron-roughness", load_image("../assets/textures/pbr/rusted_iron_2048/roughness.png", true));

        auto pbrShader = shaderMonitor.watch("../assets/shaders/textured_pbr_vert.glsl", "../assets/shaders/textured_pbr_frag.glsl");
        auto pbrMaterial = std::make_shared<MetallicRoughnessMaterial>(pbrShader);
        pbrMaterial->set_albedo_texture(texDatabase["rusted-iron-albedo"]);
        pbrMaterial->set_normal_texture(texDatabase["rusted-iron-normal"]);
        pbrMaterial->set_metallic_texture(texDatabase["rusted-iron-metallic"]);
        pbrMaterial->set_roughness_texture(texDatabase["rusted-iron-roughness"]);
        pbrMaterial->set_radiance_cubemap(texDatabase["wells-radiance-cubemap"]);
        pbrMaterial->set_irrradiance_cubemap(texDatabase["wells-irradiance-cubemap"]);
        scene.namedMaterialList["material-pbr"] = pbrMaterial;

        /*
        pbrMaterial->set_albedo_texture(load_image("../assets/models/cerberus/albedo.png", true));
        pbrMaterial->set_normal_texture(load_image("../assets/models/cerberus/normal.png", true));
        pbrMaterial->set_metallic_texture(load_image("../assets/models/cerberus/metallic.png", true));
        pbrMaterial->set_roughness_texture(load_image("../assets/models/cerberus/roughness.png", true));

        auto pbrShader = shaderMonitor.watch("../assets/shaders/textured_pbr_vert.glsl", "../assets/shaders/textured_pbr_frag.glsl");
        auto pbrMaterial = std::make_shared<MetallicRoughnessMaterial>(pbrShader);
        pbrMaterial->set_albedo_texture(load_image("../assets/models/pbr_chest/albedo.png", true));
        pbrMaterial->set_normal_texture(load_image("../assets/models/pbr_chest/normal.png", true));
        pbrMaterial->set_metallic_texture(load_image("../assets/models/pbr_chest/metallic.png", true));
        pbrMaterial->set_roughness_texture(load_image("../assets/models/pbr_chest/roughness.png", true));
        pbrMaterial->set_radiance_cubemap(radianceTex);
        pbrMaterial->set_irrradiance_cubemap(irradianceTex);
        scene.namedMaterialList["material-pbr"] = pbrMaterial;
        */

        auto geom = load_geometry_from_obj_no_texture("../assets/models/cerberus/cerberus.obj")[0];
        StaticMesh materialTestMesh;
        materialTestMesh.set_static_mesh(geom, 1.33f);
        materialTestMesh.set_pose(Pose(make_rotation_quat_axis_angle({ 0, 1, 0 }, -ANVIL_PI), float3(0, 0.75f, 0)));
        materialTestMesh.set_material(scene.namedMaterialList["material-pbr"].get());
       // scene.models.push_back(std::move(materialTestMesh));
    }

    scoped_timer t("load capsule");
    auto capsuleGeom = load_geometry_from_ply("../assets/models/geometry/CapsuleUniform.ply", true);

    {
        StaticMesh materialTestMesh;
        materialTestMesh.set_static_mesh(capsuleGeom, 0.5f);
        materialTestMesh.set_pose(Pose(float4(0, 0, 0, 1), float3(1.5, 0.33f, -0.5)));
        materialTestMesh.set_material(scene.namedMaterialList["material-pbr"].get());
        scene.models.push_back(std::move(materialTestMesh));
    }

    {
        StaticMesh materialTestMesh;
        materialTestMesh.set_static_mesh(capsuleGeom, 0.5f);
        materialTestMesh.set_pose(Pose(float4(0, 0, 0, 1), float3(1.5, 0.33f, 0.0)));
        materialTestMesh.set_material(scene.namedMaterialList["material-pbr"].get());
        scene.models.push_back(std::move(materialTestMesh));
    }

    {
        StaticMesh materialTestMesh;
        materialTestMesh.set_static_mesh(capsuleGeom, 0.5f);
        materialTestMesh.set_pose(Pose(float4(0, 0, 0, 1), float3(1.5, 0.33f, +0.5)));
        materialTestMesh.set_material(scene.namedMaterialList["material-pbr"].get());
        scene.models.push_back(std::move(materialTestMesh));
    }

    /*
    {
        scoped_timer("load torus knot");
        auto geom = load_geometry_from_ply("../assets/models/geometry/TorusKnotUniform.ply", true);
        StaticMesh materialTestMesh;
        materialTestMesh.set_static_mesh(geom, 0.5f);
        materialTestMesh.set_pose(Pose(float4(0, 0, 0, 1), float3(-1, 0.5f, 0)));
        materialTestMesh.set_material(scene.namedMaterialList["material-pbr"].get());
        scene.models.push_back(std::move(materialTestMesh));
    }

    {
        scoped_timer("load cuboid");
        auto geom = load_geometry_from_ply("../assets/models/geometry/SphereCuboidUniform.ply", true);
        StaticMesh materialTestMesh;
        materialTestMesh.set_static_mesh(geom, 0.5f);
        materialTestMesh.set_pose(Pose(float4(0, 0, 0, 1), float3(+1, 0.5f, 0)));
        materialTestMesh.set_material(scene.namedMaterialList["material-pbr"].get());
        scene.models.push_back(std::move(materialTestMesh));
    }

    {
        scoped_timer("load cone");
        auto geom = load_geometry_from_ply("../assets/models/geometry/ConeUniform.ply", true);
        StaticMesh materialTestMesh;
        materialTestMesh.set_static_mesh(geom, 0.5f);
        materialTestMesh.set_pose(Pose(float4(0, 0, 0, 1), float3(0, 0.5f, +1)));
        materialTestMesh.set_material(scene.namedMaterialList["material-pbr"].get());
        scene.models.push_back(std::move(materialTestMesh));
    }
    */

    if (hmd)
    {
        auto controllerRenderModel = hmd->get_controller_render_data();

        scene.leftController.reset(new MotionControllerVR(physicsEngine, hmd->get_controller(vr::TrackedControllerRole_LeftHand), controllerRenderModel));
        scene.rightController.reset(new MotionControllerVR(physicsEngine, hmd->get_controller(vr::TrackedControllerRole_RightHand), controllerRenderModel));

        // This section sucks I think:
        auto texturedShader = shaderMonitor.watch("../assets/shaders/textured_model_vert.glsl", "../assets/shaders/textured_model_frag.glsl");
        auto texturedMaterial = std::make_shared<TexturedMaterial>(texturedShader);
        texturedMaterial->set_diffuse_texture(controllerRenderModel->tex);
        scene.namedMaterialList["material-textured"] = texturedMaterial;

        // Create renderable controllers
        for (int i = 0; i < 2; ++i)
        {
            StaticMesh controller;
            controller.set_static_mesh(controllerRenderModel->mesh, 1.0f);
            controller.set_pose(Pose(float4(0, 0, 0, 1), float3(0, 0, 0)));
            controller.set_material(scene.namedMaterialList["material-textured"].get());
            scene.controllers.push_back(std::move(controller));
        }

        // Set up the ground plane used as a nav mesh for the parabolic pointer.
        // Doesn't need a separate renderable object (since handled by the debug grid already)
        scene.navMesh = make_plane(48, 48, 96, 96);

        // Flip nav mesh since it's not automatically the correct orientation to be a floor
        for (auto & p : scene.navMesh.vertices)
        {
            float4x4 model = make_rotation_matrix({ 1, 0, 0 }, -ANVIL_PI / 2);
            p = transform_coord(model, p);
        }

        scene.teleportationArc.set_pose(Pose(float4(0, 0, 0, 1), float3(0, 0, 0)));
        scene.teleportationArc.set_material(scene.namedMaterialList["material-textured-pbr"].get());
        scene.params.navMeshBounds = scene.navMesh.compute_bounds();
    }
}

void VirtualRealityApp::on_window_resize(int2 size)
{

}

void VirtualRealityApp::on_input(const InputEvent & event) 
{
    cameraController.handle_input(event);
    if (igm) igm->update_input(event);

    if (event.type == InputEvent::MOUSE && event.action == GLFW_PRESS)
    {
        if (event.value[0] == GLFW_MOUSE_BUTTON_LEFT)
        {
            for (auto & model : scene.models)
            {
                auto worldRay = debugCam.get_world_ray(event.cursor, float2(event.windowSize));
                RaycastResult rc = model.raycast(worldRay);

                if (rc.hit)
                {
                    std::cout << "Hit Model" << std::endl;
                }
            }
        }
    }
}

void VirtualRealityApp::on_update(const UpdateEvent & e) 
{
    cameraController.update(e.timestep_ms);

    shaderMonitor.handle_recompile();

    if (hmd)
    {
        scene.leftController->update(hmd->get_controller(vr::TrackedControllerRole_LeftHand).get_pose(hmd->get_world_pose()));
        scene.rightController->update(hmd->get_controller(vr::TrackedControllerRole_RightHand).get_pose(hmd->get_world_pose()));

        physicsEngine->update(e.timestep_ms);

        btTransform leftTranslation;
        scene.leftController->physicsObject->body->getMotionState()->getWorldTransform(leftTranslation);

        btTransform rightTranslation;
        scene.rightController->physicsObject->body->getMotionState()->getWorldTransform(rightTranslation);

        // Workaround until a nicer system is in place
        for (auto & obj : scene.physicsObjects)
        {
            for (auto & model : scene.models)
            {
                if (model.get_physics_component() == obj.get())
                {
                    btTransform trans;
                    obj->body->getMotionState()->getWorldTransform(trans);
                    model.set_pose(make_pose(trans));
                }
            }
        }

        // Update the the pose of the controller mesh we render
        scene.controllers[0].set_pose(hmd->get_controller(vr::TrackedControllerRole_LeftHand).get_pose(hmd->get_world_pose()));
        scene.controllers[1].set_pose(hmd->get_controller(vr::TrackedControllerRole_RightHand).get_pose(hmd->get_world_pose()));

        //sceneDebugRenderer.draw_axis(scene.controllers[0].get_pose());
        //sceneDebugRenderer.draw_axis(scene.controllers[1].get_pose());

        std::vector<OpenVR_Controller::ButtonState> trackpadStates = { 
            hmd->get_controller(vr::TrackedControllerRole_LeftHand).pad, 
            hmd->get_controller(vr::TrackedControllerRole_RightHand).pad };

        // Todo: refactor
        for (int i = 0; i < trackpadStates.size(); ++i)
        {
            const auto state = trackpadStates[i];

            if (state.down)
            {
                auto pose = hmd->get_controller(vr::ETrackedControllerRole(i + 1)).get_pose(hmd->get_world_pose());
                scene.params.position = pose.position;
                scene.params.forward = -qzdir(pose.orientation);

                Geometry pointerGeom;
                if (make_parabolic_pointer(scene.params, pointerGeom, scene.teleportLocation))
                {
                    scene.needsTeleport = true;
                    scene.teleportationArc.set_static_mesh(pointerGeom, 1.f, GL_DYNAMIC_DRAW);
                }
            }

            if (state.released && scene.needsTeleport)
            {
                scene.needsTeleport = false;

                scene.teleportLocation.y = hmd->get_hmd_pose().position.y;
                Pose teleportPose(hmd->get_hmd_pose().orientation, scene.teleportLocation);

                hmd->set_world_pose({}); // reset world pose
                auto hmd_pose = hmd->get_hmd_pose(); // pose is now in the HMD's own coordinate system
                hmd->set_world_pose(teleportPose * hmd_pose.inverse());

                Geometry emptyGeom;
                scene.teleportationArc.set_static_mesh(emptyGeom, GL_DYNAMIC_DRAW);
            }
        }

    }

    static float angle = 0.f;
    scene.pointLights[0].position = float3(1.5 * sin(angle), 1.5f, 1.5 * cos(angle));
    scene.pointLights[1].position = float3(1.5 * sin(-angle), 1.5f, 1.5 * cos(-angle));
    angle += 0.01f;

    // Iterate scene and make objects visible to the renderer
    std::vector<Renderable *> renderables;
    LightCollection lightCollection;
    scene.gather(renderables, lightCollection);

    renderer->add_renderables(renderables);
    renderer->set_lights(lightCollection);

    renderer->add_debug_renderable(&scene.grid);
    renderer->sceneDebugRenderer.draw_sphere(Pose(scene.pointLights[0].position), 0.1f, float3(0, 1, 0));
    renderer->sceneDebugRenderer.draw_sphere(Pose(scene.pointLights[1].position), 0.1f, float3(0, 0, 1));
}

void VirtualRealityApp::on_draw()
{
    glfwMakeContextCurrent(window);

    if (igm) igm->begin_frame();

    int width, height;
    glfwGetWindowSize(window, &width, &height);
    glViewport(0, 0, width, height);

    gui::imgui_menu_stack menu(*this, igm->capturedKeys);

    menu.app_menu_begin();
    {
        menu.begin("File");
        if (menu.item("Exit", GLFW_MOD_ALT, GLFW_KEY_F4)) exit();
        menu.end();
    }
    menu.app_menu_end();

    physicsEngine->get_world()->debugDrawWorld();

    renderer->add_debug_renderable(physicsDebugRenderer.get());

    if (hmd)
    {
        gpuTimer.start();
        EyeData left = { hmd->get_eye_pose(vr::Hmd_Eye::Eye_Left), hmd->get_proj_matrix(vr::Hmd_Eye::Eye_Left, 0.01, 25.f) };
        EyeData right = { hmd->get_eye_pose(vr::Hmd_Eye::Eye_Right), hmd->get_proj_matrix(vr::Hmd_Eye::Eye_Right, 0.01, 25.f) };
        renderer->set_eye_data(left, right);
        renderer->render_frame();
        gpuTimer.stop();
        hmd->submit(renderer->get_eye_texture(Eye::LeftEye), renderer->get_eye_texture(Eye::RightEye));
        hmd->update();

        // Todo: derive center eye
        debugCam.set_pose(hmd->get_eye_pose(vr::Hmd_Eye::Eye_Left));
    }
    else
    {
        const float4x4 projMatrix = debugCam.get_projection_matrix(float(width) / float(height));
        const EyeData centerEye = { debugCam.get_pose(), projMatrix };
        renderer->set_eye_data(centerEye, centerEye);
        renderer->render_frame();
    }

    gui::imgui_fixed_window_begin("Render Debug Views", { { 0, height - 220 }, { width, height } });
    gui::Img(renderer->bloom->get_luminance_texture(), "Luminance", { 240, 180 }); ImGui::SameLine();
    gui::Img(renderer->bloom->get_bright_tex(), "Bright", { 240, 180 }); ImGui::SameLine();
    gui::Img(renderer->bloom->get_blur_tex(), "Blur", { 240, 180 }); ImGui::SameLine();
    gui::Img(renderer->bloom->get_output_texture(), "Output", { 240, 180 }); ImGui::SameLine();
    gui::imgui_fixed_window_end();

    Bounds2D rect{ { 0.f, 0.f },{ (float)width,(float)height } };

    const float mid = (rect.min().x + rect.max().x) / 2.f;
    ScreenViewport leftviewport = { rect.min(),{ mid - 2.f, rect.max().y }, renderer->get_eye_texture(Eye::LeftEye) };
    ScreenViewport rightViewport = { { mid + 2.f, rect.min().y }, rect.max(), renderer->get_eye_texture(Eye::RightEye) };

    viewports.clear();
    viewports.push_back(leftviewport);
    viewports.push_back(rightViewport);

    if (viewports.size())
    {
        glUseProgram(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    for (auto & v : viewports)
    {
        glViewport(v.bmin.x, height - v.bmax.y, v.bmax.x - v.bmin.x, v.bmax.y - v.bmin.y);
        glActiveTexture(GL_TEXTURE0);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, v.texture);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(-1, -1);
        glTexCoord2f(1, 0); glVertex2f(+1, -1);
        glTexCoord2f(1, 1); glVertex2f(+1, +1);
        glTexCoord2f(0, 1); glVertex2f(-1, +1);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    }

    physicsDebugRenderer->clear();
    
    const auto hpose = hmd->get_hmd_pose();

    ImGui::Text("Render Frame: %f", gpuTimer.elapsed_ms());
    ImGui::Text("Head Pose: %f, %f, %f", hpose.position.x, hpose.position.y, hpose.position.z);

    if (igm) igm->end_frame();

    glfwSwapBuffers(window);
    gl_check_error(__FILE__, __LINE__);
}

int main(int argc, char * argv[])
{
    VirtualRealityApp app;
    app.main_loop();
    return EXIT_SUCCESS;
}