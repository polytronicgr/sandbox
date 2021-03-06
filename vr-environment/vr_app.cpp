#include "vr_app.hpp"
#include "gl-imgui.hpp"
#include "asset_io.hpp"
#include "math-core.hpp"

VirtualRealityApp::VirtualRealityApp() : GLFWApp(1280, 800, "VR Sandbox")
{
    scoped_timer t("constructor");

    int windowWidth, windowHeight;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);

    igm.reset(new gui::imgui_wrapper(window));

    cameraController.set_camera(&debugCam);

    // Initialize Bullet physics
    setup_physics();

    try
    {
        hmd.reset(new OpenVR_HMD());
        const uint2 targetSize = hmd->get_recommended_render_target_size();
        glfwSwapInterval(0);

        auto controllerRenderModel = hmd->get_controller_render_data();
        scene.leftController.reset(new MotionControllerVR(physicsEngine, hmd->get_controller(vr::TrackedControllerRole_LeftHand), controllerRenderModel));
        scene.rightController.reset(new MotionControllerVR(physicsEngine, hmd->get_controller(vr::TrackedControllerRole_RightHand), controllerRenderModel));
    }
    catch (const std::exception & e)
    {
        std::cout << "OpenVR Exception: " << e.what() << std::endl;
    }


    gl_check_error(__FILE__, __LINE__);
}

VirtualRealityApp::~VirtualRealityApp()
{
    hmd.reset();
}

void VirtualRealityApp::setup_physics()
{
    AVL_SCOPED_TIMER("setup_physics");

    physicsEngine.reset(new BulletEngineVR());

    physicsDebugRenderer.reset(new PhysicsDebugRenderer()); // Sets up a few gl objects
    physicsDebugRenderer->setDebugMode(
        btIDebugDraw::DBG_DrawWireframe |
        btIDebugDraw::DBG_DrawContactPoints |
        btIDebugDraw::DBG_DrawConstraints |
        btIDebugDraw::DBG_DrawConstraintLimits |
        btIDebugDraw::DBG_DrawFeaturesText | 
        btIDebugDraw::DBG_DrawText);

    // Allow bullet world to make calls into our debug renderer
    physicsEngine->get_world()->setDebugDrawer(physicsDebugRenderer.get());
}

void VirtualRealityApp::on_window_resize(int2 size)
{
    int windowWidth, windowHeight;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
}

void VirtualRealityApp::on_input(const InputEvent & event) 
{
    cameraController.handle_input(event);
    if (igm) igm->update_input(event);
}

void VirtualRealityApp::on_update(const UpdateEvent & e) 
{
    cameraController.update(e.timestep_ms);

    shaderMonitor.handle_recompile();

    if (hmd)
    {
        scene.leftController->update(hmd->get_controller(vr::TrackedControllerRole_LeftHand)->get_pose(hmd->get_world_pose()));
        scene.rightController->update(hmd->get_controller(vr::TrackedControllerRole_RightHand)->get_pose(hmd->get_world_pose()));

        physicsEngine->update(e.timestep_ms);

        btTransform leftTranslation;
        scene.leftController->physicsObject->body->getMotionState()->getWorldTransform(leftTranslation);

        btTransform rightTranslation;
        scene.rightController->physicsObject->body->getMotionState()->getWorldTransform(rightTranslation);

        // Workaround until a nicer system is in place
        for (auto & obj : scene.physicsObjects)
        {
            /*
            for (auto & model : scene.models)
            {
                if (model.get_physics_component() == obj.get())
                {
                    btTransform trans;
                    obj->body->getMotionState()->getWorldTransform(trans);
                    model.set_pose(make_pose(trans));
                }
            }
            */
        }

        // Update the the pose of the controller mesh we render
        // scene.controllers[0].set_pose(hmd->get_controller(vr::TrackedControllerRole_LeftHand).get_pose(hmd->get_world_pose()));
        // scene.controllers[1].set_pose(hmd->get_controller(vr::TrackedControllerRole_RightHand).get_pose(hmd->get_world_pose()));

        //sceneDebugRenderer.draw_axis(scene.controllers[0].get_pose());
        //sceneDebugRenderer.draw_axis(scene.controllers[1].get_pose());

        std::vector<OpenVR_Controller::ButtonState> trackpadStates = { 
            hmd->get_controller(vr::TrackedControllerRole_LeftHand)->pad, 
            hmd->get_controller(vr::TrackedControllerRole_RightHand)->pad 
        };

        // Todo: refactor
        for (int i = 0; i < trackpadStates.size(); ++i)
        {
            const auto state = trackpadStates[i];

            if (state.down)
            {
                auto pose = hmd->get_controller(vr::ETrackedControllerRole(i + 1))->get_pose(hmd->get_world_pose());
                scene.params.position = pose.position;
                scene.params.forward = -qzdir(pose.orientation);

                Geometry pointerGeom;
                if (make_parabolic_pointer(scene.params, pointerGeom, scene.teleportLocation))
                {
                    scene.needsTeleport = true;
                    //scene.teleportationArc.set_static_mesh(pointerGeom, 1.f, GL_DYNAMIC_DRAW);
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
                //scene.teleportationArc.set_static_mesh(emptyGeom, GL_DYNAMIC_DRAW);
            }
        }

    }

    if (hmd)
    {
        std::vector<OpenVR_Controller::ButtonState> triggerStates = {
            hmd->get_controller(vr::TrackedControllerRole_LeftHand)->trigger,
            hmd->get_controller(vr::TrackedControllerRole_RightHand)->trigger
        };
    }

}

void VirtualRealityApp::on_draw()
{
    glfwMakeContextCurrent(window);

    if (igm) igm->begin_frame();

    int width, height;
    glfwGetWindowSize(window, &width, &height);
    glViewport(0, 0, width, height);

    physicsEngine->get_world()->debugDrawWorld();

    /*
    Bounds2D rect{ { 0.f, 0.f },{ (float)width,(float)height } };
    const float mid = (rect.min().x + rect.max().x) / 2.f;
    ScreenViewport leftviewport = { rect.min(),{ mid - 2.f, rect.max().y }, renderer->get_eye_texture(Eye::LeftEye) };
    ScreenViewport rightViewport = { { mid + 2.f, rect.min().y }, rect.max(), renderer->get_eye_texture(Eye::RightEye) };
    viewports.clear();
    viewports.push_back(leftviewport);
    viewports.push_back(rightViewport);
    */

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

    ImGui::Text("Render Frame: %f", gpuTimer.elapsed_ms());

    if (hmd)
    {
        const auto headPose = hmd->get_hmd_pose();
        ImGui::Text("Head Pose: %f, %f, %f", headPose.position.x, headPose.position.y, headPose.position.z);
    }

    if (igm) igm->end_frame();

    glfwSwapBuffers(window);

    frameCount++;

    gl_check_error(__FILE__, __LINE__);
}

int main(int argc, char * argv[])
{
    VirtualRealityApp app;
    app.main_loop();
    return EXIT_SUCCESS;
}
