#include "index.hpp"

// 1. GL_FRAMEBUFFER_SRGB?
// 2. Proper luminance downsampling
// 3. Blitting? / glReadPixels
// 4.
// 5.

// http://www.gamedev.net/topic/674450-hdr-rendering-average-luminance/

void luminance_offset_2x2(GlShader * shader, float2 size)
{
    float4 offsets[16];
    
    float du = 1.0f / size.x;
    float dv = 1.0f / size.y;
    
    uint32_t num = 0;
    for (uint32_t yy = 0; yy < 3; ++yy)
    {
        for (uint32_t xx = 0; xx < 3; ++xx)
        {
            offsets[num][0] = (xx) * du;
            offsets[num][1] = (yy) * dv;
            ++num;
        }
    }

    for (int n = 0; n < num; ++n)
    {
        shader->uniform("u_offset[" + std::to_string(n) + "]", offsets[n]);
    }
}

void luminance_offset_4x4(GlShader * shader, float2 size)
{
    float4 offsets[16];
    
    float du = 1.0f / size.x;
    float dv = 1.0f / size.y;
    
    uint32_t num = 0;
    for (uint32_t yy = 0; yy < 4; ++yy)
    {
        for (uint32_t xx = 0; xx < 4; ++xx)
        {
            offsets[num][0] = (xx - 1.0f) * du;
            offsets[num][1] = (yy - 1.0f) * dv;
            ++num;
        }
    }
    
    for (int n = 0; n < num; ++n)
        shader->uniform("u_offset[" + std::to_string(n) + "]", offsets[n]);
}

std::shared_ptr<GlShader> make_watched_shader(ShaderMonitor & mon, const std::string vertexPath, const std::string fragPath)
{
    auto shader = std::make_shared<GlShader>(read_file_text(vertexPath), read_file_text(fragPath));
    mon.add_shader(shader, vertexPath, fragPath);
    return shader;
}

struct ExperimentalApp : public GLFWApp
{
    uint64_t frameCount = 0;

    GlCamera camera;
    HosekProceduralSky skydome;
    RenderableGrid grid;
    FlyCameraController cameraController;
    
    std::vector<Renderable> models;
    std::vector<LightObject> lights;
    
    UIComponent uiSurface;
    
    float middleGrey = 0.18f;
    float whitePoint = 1.1f;
    float threshold = 1.5f;
    float time = 0.0f;
    
    ShaderMonitor shaderMonitor;
    
    std::shared_ptr<GlShader> hdr_meshShader;
    
    std::shared_ptr<GlShader> hdr_lumShader;
    std::shared_ptr<GlShader> hdr_avgLumShader;
    std::shared_ptr<GlShader> hdr_blurShader;
    std::shared_ptr<GlShader> hdr_brightShader;
    std::shared_ptr<GlShader> hdr_tonemapShader;
    
    std::shared_ptr<GLTextureView> luminanceView;
    std::shared_ptr<GLTextureView> averageLuminanceView;
    std::shared_ptr<GLTextureView> brightnessView;
    std::shared_ptr<GLTextureView> blurView;
    std::shared_ptr<GLTextureView> tonemapView;
    std::shared_ptr<GLTextureView> middleGreyView;
    
    GlMesh fullscreen_post_quad;
    
    GlTexture       middleGreyTex;
    
    GlTexture       sceneColorTexture;
    GlTexture       sceneDepthTexture;
    GlFramebuffer   sceneFramebuffer;
    
    GlTexture       luminanceTex_0;
    GlFramebuffer   luminance_0;
    
    GlTexture       luminanceTex_1;
    GlFramebuffer   luminance_1;
    
    GlTexture       luminanceTex_2;
    GlFramebuffer   luminance_2;
    
    GlTexture       luminanceTex_3;
    GlFramebuffer   luminance_3;
    
    GlTexture       luminanceTex_4;
    GlFramebuffer   luminance_4;
    
    GlTexture       brightTex;
    GlFramebuffer   brightFramebuffer;
    
    GlTexture       blurTex;
    GlFramebuffer   blurFramebuffer;
    
    GlTexture       emptyTex;

    ExperimentalApp() : GLFWApp(1280, 720, "HDR Bloom App")
    {
        glEnable(GL_FRAMEBUFFER_SRGB);
        
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        glViewport(0, 0, width, height);
        
        fullscreen_post_quad = make_fullscreen_quad();
        
        std::vector<float> greenDebugPixel = {0.f, 1.0f, 0.f, 1.0f};
        
        std::vector<uint8_t> white;
        
        for (int i = 0; i < width * height; i++)
        {
            white.push_back(255);
            white.push_back(255);
            white.push_back(255);
            white.push_back(255);
        }
        
        // Debugging views
        uiSurface.bounds = {0, 0, (float) width, (float) height};
        uiSurface.add_child( {{0.0000, +10},{0, +10},{0.1667, -10},{0.133, +10}}, std::make_shared<UIComponent>());
        uiSurface.add_child( {{0.1667, +10},{0, +10},{0.3334, -10},{0.133, +10}}, std::make_shared<UIComponent>());
        uiSurface.add_child( {{0.3334, +10},{0, +10},{0.5009, -10},{0.133, +10}}, std::make_shared<UIComponent>());
        uiSurface.add_child( {{0.5000, +10},{0, +10},{0.6668, -10},{0.133, +10}}, std::make_shared<UIComponent>());
        uiSurface.add_child( {{0.6668, +10},{0, +10},{0.8335, -10},{0.133, +10}}, std::make_shared<UIComponent>());
        uiSurface.add_child( {{0.8335, +10},{0, +10},{1.0000, -10},{0.133, +10}}, std::make_shared<UIComponent>());
        uiSurface.layout();
        
        sceneColorTexture.load_data(width, height, GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_BYTE, white.data());
        sceneDepthTexture.load_data(width, height, GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT, GL_FLOAT, white.data());
        
        luminanceTex_0.load_data(128, 128, GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_BYTE, white.data());
        luminanceTex_1.load_data(64, 64,   GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_BYTE, white.data());
        luminanceTex_2.load_data(16, 16,   GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_BYTE, white.data());
        luminanceTex_3.load_data(4, 4,     GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_BYTE, white.data());
        luminanceTex_4.load_data(1, 1,     GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_BYTE, white.data());
        
        // GL_RGBA8, GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_BYTE, false }, // BGRA8
        
        brightTex.load_data(width / 2, height / 2, GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_BYTE, white.data());
        blurTex.load_data(width / 8, height / 8, GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_BYTE, white.data());
    
        sceneFramebuffer.attach(GL_COLOR_ATTACHMENT0, sceneColorTexture);
        sceneFramebuffer.attach(GL_DEPTH_ATTACHMENT, sceneDepthTexture);
        if (!sceneFramebuffer.check_complete()) throw std::runtime_error("incomplete scene framebuffer");
        
        luminance_0.attach(GL_COLOR_ATTACHMENT0, luminanceTex_0);
        if (!luminance_0.check_complete()) throw std::runtime_error("incomplete lum0 framebuffer");
        
        luminance_1.attach(GL_COLOR_ATTACHMENT0, luminanceTex_1);
        if (!luminance_1.check_complete()) throw std::runtime_error("incomplete lum1 framebuffer");
        
        luminance_2.attach(GL_COLOR_ATTACHMENT0, luminanceTex_2);
        if (!luminance_2.check_complete()) throw std::runtime_error("incomplete lum2 framebuffer");
        
        luminance_3.attach(GL_COLOR_ATTACHMENT0, luminanceTex_3);
        if (!luminance_3.check_complete()) throw std::runtime_error("incomplete lum3 framebuffer");
        
        luminance_4.attach(GL_COLOR_ATTACHMENT0, luminanceTex_4);
        if (!luminance_4.check_complete()) throw std::runtime_error("incomplete lum4 framebuffer");
        
        brightFramebuffer.attach(GL_COLOR_ATTACHMENT0, brightTex);
        if (!brightFramebuffer.check_complete()) throw std::runtime_error("incomplete bright framebuffer");
        
        blurFramebuffer.attach(GL_COLOR_ATTACHMENT0, blurTex);
        if (!blurFramebuffer.check_complete()) throw std::runtime_error("incomplete blur framebuffer");
        
        middleGreyTex.load_data(1, 1, GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        
        luminanceView.reset(new GLTextureView(luminanceTex_0.get_gl_handle()));
        averageLuminanceView.reset(new GLTextureView(luminanceTex_4.get_gl_handle()));
        brightnessView.reset(new GLTextureView(brightTex.get_gl_handle()));
        blurView.reset(new GLTextureView(blurTex.get_gl_handle()));
        tonemapView.reset(new GLTextureView(sceneColorTexture.get_gl_handle()));
        middleGreyView.reset(new GLTextureView(middleGreyTex.get_gl_handle()));
        
        cameraController.set_camera(&camera);
        
        camera.look_at({0, 8, 24}, {0, 0, 0});
        
        // Scene shaders
        hdr_meshShader = make_watched_shader(shaderMonitor, "assets/shaders/simple_vert.glsl", "assets/shaders/simple_frag.glsl");

        // Pipeline shaders
        hdr_lumShader = make_watched_shader(shaderMonitor, "assets/shaders/hdr/hdr_lum_vert.glsl", "assets/shaders/hdr/hdr_lum_frag.glsl");
        hdr_avgLumShader = make_watched_shader(shaderMonitor, "assets/shaders/hdr/hdr_lumavg_vert.glsl", "assets/shaders/hdr/hdr_lumavg_frag.glsl");
        hdr_blurShader = make_watched_shader(shaderMonitor, "assets/shaders/hdr/hdr_blur_vert.glsl", "assets/shaders/hdr/hdr_blur_frag.glsl");
        hdr_brightShader = make_watched_shader(shaderMonitor, "assets/shaders/hdr/hdr_bright_vert.glsl", "assets/shaders/hdr/hdr_bright_frag.glsl");
        hdr_tonemapShader = make_watched_shader(shaderMonitor, "assets/shaders/hdr/hdr_tonemap_vert.glsl", "assets/shaders/hdr/hdr_tonemap_frag.glsl");
        
        std::vector<uint8_t> pixel = {255, 255, 255, 255};
        emptyTex.load_data(1, 1, GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        
        lights.resize(2);
        lights[0].color = float3(249.f / 255.f, 228.f / 255.f, 157.f / 255.f);
        lights[0].pose.position = float3(25, 15, 0);
        lights[1].color = float3(255.f / 255.f, 242.f / 255.f, 254.f / 255.f);
        lights[1].pose.position = float3(-25, 15, 0);
        
        models.push_back(Renderable(make_icosahedron()));
        
        grid = RenderableGrid(1, 64, 64);

        gl_check_error(__FILE__, __LINE__);
    }
    
    void on_window_resize(int2 size) override
    {

    }
    
    void on_input(const InputEvent & event) override
    {
        cameraController.handle_input(event);
        
        if (event.type == InputEvent::KEY)
        {
            if (event.value[0] == GLFW_KEY_SPACE && event.action == GLFW_RELEASE)
            {
                
            }

        }
        
        if (event.type == InputEvent::MOUSE && event.action == GLFW_PRESS)
        {
            if (event.value[0] == GLFW_MOUSE_BUTTON_LEFT)
            {

            }
        }
    }
    
    void on_update(const UpdateEvent & e) override
    {
        cameraController.update(e.timestep_ms);
        time += e.timestep_ms / 1000;
        shaderMonitor.handle_recompile();
    }
    
    float decodeRE8(float4 re8)
    {
        float exponent = re8.w * 255.0 - 128.0;
        return re8.x * exp2(exponent);
    }
    
    void on_draw() override
    {
        glfwMakeContextCurrent(window);
        
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);

        //glDisable(GL_BLEND);
        
        glEnable(GL_FRAMEBUFFER_SRGB);
        
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        glViewport(0, 0, width, height);
     
        // Initial clear
        glClearColor(0.0f, 0.00f, 0.00f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const auto proj = camera.get_projection_matrix((float) width / (float) height);
        const float4x4 view = camera.get_view_matrix();
        const float4x4 viewProj = mul(proj, view);
        //const float4x4 modelViewProj = viewProj;
        
        gl_check_error(__FILE__, __LINE__);
        
        // Render skybox into scene
        sceneFramebuffer.bind_to_draw();
        glClear(GL_DEPTH_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // Clear anything out of default fbo
        skydome.render(viewProj, camera.get_eye_point(), camera.farClip);
        
        {
            hdr_meshShader->bind();
            
            hdr_meshShader->uniform("u_eye", camera.get_eye_point());
            hdr_meshShader->uniform("u_viewProj", viewProj);
            
            hdr_meshShader->uniform("u_emissive", float3(.10f, 0.10f, 0.10f));
            hdr_meshShader->uniform("u_diffuse", float3(0.4f, 0.425f, 0.415f));
            hdr_meshShader->uniform("useNormal", 0);
            
            for (int i = 0; i < lights.size(); i++)
            {
                auto light = lights[i];
                hdr_meshShader->uniform("u_lights[" + std::to_string(i) + "].position", light.pose.position);
                hdr_meshShader->uniform("u_lights[" + std::to_string(i) + "].color", light.color);
            }
            
            for (const auto & model : models)
            {
                hdr_meshShader->uniform("u_modelMatrix", model.get_model());
                hdr_meshShader->uniform("u_modelMatrixIT", inv(transpose(model.get_model())));
                model.draw();
            }
            
            gl_check_error(__FILE__, __LINE__);
            
            hdr_meshShader->unbind();
        }
        
        grid.render(proj, view);
        
        gl_check_error(__FILE__, __LINE__);
        
        // Disable culling and depth testing for post processing
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        
        std::vector<float> lumValue = {0, 0, 0, 0};
        {
            luminance_0.bind_to_draw(); // 128x128 surface area - calculate luminance
            hdr_lumShader->bind();
            luminance_offset_2x2(hdr_lumShader.get(), float2(128, 128));
            hdr_lumShader->texture("s_texColor", 0, sceneColorTexture);
            hdr_lumShader->uniform("u_modelViewProj", Identity4x4);
            fullscreen_post_quad.draw_elements();
            hdr_lumShader->unbind();

            gl_check_error(__FILE__, __LINE__);
            
            luminance_1.bind_to_draw(); // 64x64 surface area - downscale + average
            hdr_avgLumShader->bind();
            luminance_offset_4x4(hdr_avgLumShader.get(), float2(128, 128));
            hdr_avgLumShader->texture("s_texColor", 0, luminanceTex_0);
            hdr_avgLumShader->uniform("u_modelViewProj", Identity4x4);
            fullscreen_post_quad.draw_elements();
            hdr_avgLumShader->unbind();
            
            gl_check_error(__FILE__, __LINE__);
            
            luminance_2.bind_to_draw(); // 16x16 surface area - downscale + average
            hdr_avgLumShader->bind();
            luminance_offset_4x4(hdr_avgLumShader.get(), float2(64, 64));
            hdr_avgLumShader->texture("s_texColor", 0, luminanceTex_1);
            hdr_avgLumShader->uniform("u_modelViewProj", Identity4x4);
            fullscreen_post_quad.draw_elements();
            hdr_avgLumShader->unbind();
            
            gl_check_error(__FILE__, __LINE__);
            
            luminance_3.bind_to_draw(); // 4x4 surface area - downscale + average
            hdr_avgLumShader->bind();
            luminance_offset_4x4(hdr_avgLumShader.get(), float2(16, 16));
            hdr_avgLumShader->texture("s_texColor", 0, luminanceTex_2);
            hdr_avgLumShader->uniform("u_modelViewProj", Identity4x4);
            fullscreen_post_quad.draw_elements();
            hdr_avgLumShader->unbind();
            
            gl_check_error(__FILE__, __LINE__);
            
            luminance_4.bind_to_draw(); // 1x1 surface area - downscale + average
            hdr_avgLumShader->bind();
            luminance_offset_4x4(hdr_avgLumShader.get(), float2(4, 4));
            hdr_avgLumShader->texture("s_texColor", 0, luminanceTex_3);
            hdr_avgLumShader->uniform("u_modelViewProj", Identity4x4);
            fullscreen_post_quad.draw_elements();
            hdr_avgLumShader->unbind();
            
            gl_check_error(__FILE__, __LINE__);
            
            // Read luminance value
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, luminanceTex_4.get_gl_handle());
            glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, lumValue.data());
            
            gl_check_error(__FILE__, __LINE__);
        }
        
        float4 tonemap = { middleGrey, whitePoint * whitePoint, threshold, time };
        
        // Take original scene framebuffer and render for brightness
        gl_check_error(__FILE__, __LINE__);

        brightFramebuffer.bind_to_draw(); // 1/2 size
        hdr_brightShader->bind();
        //luminance_offset_4x4(hdr_brightShader.get(), float2(width / 2.f, height / 2.f));
        hdr_brightShader->texture("s_texColor", 0 , sceneColorTexture);
        hdr_brightShader->texture("s_texLum", 1, luminanceTex_4); // 1x1
        hdr_brightShader->uniform("u_tonemap", tonemap);
        hdr_brightShader->uniform("u_modelViewProj", Identity4x4);
        fullscreen_post_quad.draw_elements();
        hdr_brightShader->unbind();
        
        gl_check_error(__FILE__, __LINE__);
        
        blurFramebuffer.bind_to_draw(); // 1/8 size
        hdr_blurShader->bind();
        hdr_blurShader->texture("s_texColor", 0, brightTex);
        hdr_blurShader->uniform("u_viewTexel", float2(1.f / (width / 8.f), 1.f / (height / 8.f)));
        hdr_blurShader->uniform("u_modelViewProj", Identity4x4);
        fullscreen_post_quad.draw_elements();
        hdr_blurShader->unbind();

        gl_check_error(__FILE__, __LINE__);

        // Output to default screen framebuffer on the last pass
        //glDisable(GL_FRAMEBUFFER_SRGB);
        
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        
        hdr_tonemapShader->bind();
        hdr_tonemapShader->texture("s_texColor", 0, sceneColorTexture);
        hdr_tonemapShader->texture("s_texLum", 1, luminanceTex_4); // 1x1
        hdr_tonemapShader->texture("s_texBlur", 2, blurTex);
        hdr_tonemapShader->uniform("u_tonemap", tonemap);
        hdr_tonemapShader->uniform("u_modelViewProj", Identity4x4);
        hdr_tonemapShader->uniform("u_viewTexel", float2(1.f/ width, 1.f / height));
        hdr_tonemapShader->uniform("u_tonemap", tonemap);
        
        fullscreen_post_quad.draw_elements();
        
        hdr_tonemapShader->unbind();
    
        gl_check_error(__FILE__, __LINE__);
        
        std::cout << decodeRE8(float4(lumValue[0], lumValue[1], lumValue[2], lumValue[3])) << std::endl;
        std::cout << tonemap << std::endl;
        
        //middleGreyTex.load_data(1, 1, GL_RGB32F, GL_RGBA, GL_FLOAT, lumValue.data());
        
        gl_check_error(__FILE__, __LINE__);
        
        {
            // Debug Draw
            luminanceView->draw(uiSurface.children[0]->bounds, int2(width, height));
            averageLuminanceView->draw(uiSurface.children[1]->bounds, int2(width, height));
            brightnessView->draw(uiSurface.children[2]->bounds, int2(width, height));
            blurView->draw(uiSurface.children[3]->bounds, int2(width, height));
            tonemapView->draw(uiSurface.children[4]->bounds, int2(width, height));
            
            //middleGreyView->draw(uiSurface.children[4]->bounds, int2(width, height));

        }
        
        gl_check_error(__FILE__, __LINE__);
        
        
        glfwSwapBuffers(window);
        
        frameCount++;
    }
    
};
