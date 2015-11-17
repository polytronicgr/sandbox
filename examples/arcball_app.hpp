#include "anvil.hpp"

using namespace math;
using namespace util;
using namespace gfx;

struct Object
{
    Pose pose;
    float3 scale;
    Object() : scale(1, 1, 1) {}
    float4x4 get_model() const { return mul(pose.matrix(), make_scaling_matrix(scale)); }
    math::Box<float, 3> bounds;
};

struct ModelObject : public Object
{
    GlMesh mesh;
    void draw() const { mesh.draw_elements(); };
};

struct ExperimentalApp : public GLFWApp
{
    
    ModelObject crateModel;
    Geometry crateGeometry;
    
    GlTexture crateDiffuseTex;
    
    std::unique_ptr<GlShader> simpleTexturedShader;
    
    GlCamera camera;
    Sphere cameraSphere;
    Arcball myArcball;
    
    float2 lastCursor;
    bool isDragging = false;
    
    ExperimentalApp() : GLFWApp(600, 600, "Arcball Camera App")
    {
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        glViewport(0, 0, width, height);
        
        crateGeometry = make_cube();
        crateModel.mesh = make_mesh_from_geometry(crateGeometry);
        crateModel.bounds = crateGeometry.compute_bounds();
        crateModel.pose.position = {0, 0, 0};
        
        simpleTexturedShader.reset(new gfx::GlShader(read_file_text("assets/shaders/simple_texture_vert.glsl"), read_file_text("assets/shaders/simple_texture_frag.glsl")));
        crateDiffuseTex = load_image("assets/models/crate/crate_diffuse.png");
        
        gfx::gl_check_error(__FILE__, __LINE__);
        
        cameraSphere = Sphere(crateModel.bounds.center(), crateModel.bounds.volume());
        myArcball = Arcball(&camera, cameraSphere);
        
        camera.look_at({0, 0, 10}, {0, 0, 0});
        //myArcball.set_constraint_axis(float3(0, 1, 0));
        
        gfx::gl_check_error(__FILE__, __LINE__);
    }
    
    void on_window_resize(math::int2 size) override
    {
        
    }
    
    void on_input(const InputEvent & event) override
    {
        
        if (event.type == InputEvent::CURSOR && isDragging)
        {
            if (event.cursor != lastCursor)
            {
                myArcball.mouse_drag(event.cursor, event.windowSize);
            }
        }
        
        if (event.type == InputEvent::MOUSE)
        {
            if (event.is_mouse_down())
            {
                isDragging = true;
                myArcball.mouse_down(event.cursor, event.windowSize);
            }
            
            if (event.is_mouse_up())
            {
                isDragging = false;
            }
        }
        
        lastCursor = event.cursor;
    }
    
    void on_update(const UpdateEvent & e) override
    {
        //if (isDragging)
            crateModel.pose.orientation = qmul(myArcball.get_quat(), crateModel.pose.orientation);
    }
    
    void on_draw() override
    {
        glfwMakeContextCurrent(window);
        
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
        
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        glViewport(0, 0, width, height);
        
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        
        const auto proj = camera.get_projection_matrix((float) width / (float) height);
        const float4x4 view = camera.get_view_matrix();
        const float4x4 viewProj = mul(proj, view);
        
        {
            simpleTexturedShader->bind();
            
            simpleTexturedShader->uniform("u_viewProj", viewProj);
            simpleTexturedShader->uniform("u_eye", camera.get_eye_point());
            
            simpleTexturedShader->uniform("u_emissive", float3(.33f, 0.36f, 0.275f));
            simpleTexturedShader->uniform("u_diffuse", float3(0.2f, 0.4f, 0.25f));
            
            simpleTexturedShader->uniform("u_lights[0].position", float3(5, 10, -5));
            simpleTexturedShader->uniform("u_lights[0].color", float3(0.7f, 0.2f, 0.2f));
            
            simpleTexturedShader->uniform("u_lights[1].position", float3(-5, 10, 5));
            simpleTexturedShader->uniform("u_lights[1].color", float3(0.4f, 0.8f, 0.4f));
            
            simpleTexturedShader->texture("u_diffuseTex", 0, crateDiffuseTex.get_gl_handle(), GL_TEXTURE_2D);
            
            {
                auto model = crateModel.get_model();
                simpleTexturedShader->uniform("u_modelMatrix", model);
                simpleTexturedShader->uniform("u_modelMatrixIT", inv(transpose(model)));
                crateModel.draw();
            }
            
            simpleTexturedShader->unbind();
        }
        
        gfx::gl_check_error(__FILE__, __LINE__);
        
        glfwSwapBuffers(window);
    }
    
};