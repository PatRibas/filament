/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "common/arguments.h"

#include "generated/resources/resources.h"

#include <filamentapp/AssetLoader.h>
#include <filamentapp/FilamentApp2.h>

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>

#include <utils/EntityManager.h>

#include <samples/SampleConfig.h>

#include <memory>

using namespace filament;
using utils::Entity;
using utils::EntityManager;

namespace {

static constexpr uint32_t TEXTURE_SIZE = 512;
static constexpr math::float3 QUAD_VERTICES[] = {
    { -0.5f, -0.5f, 0.0f },
    { 0.5f, -0.5f, 0.0f },
    { -0.5f, 0.5f, 0.0f },
    { 0.5f, -0.5f, 0.0f },
    { 0.5f, 0.5f, 0.0f },
    { -0.5f, 0.5f, 0.0f },
};

struct App {
    FilamentApp2* filamentApp = nullptr;
    SampleConfig config;
    Camera* camera = nullptr;
    Entity cameraEntity;
    Entity renderable;
    Material* computeMaterial = nullptr;
    MaterialInstance* computeInstance = nullptr;
    Material* displayMaterial = nullptr;
    MaterialInstance* displayInstance = nullptr;
    Texture* texture = nullptr;
    VertexBuffer* vertexBuffer = nullptr;
    Skybox* skybox = nullptr;
    math::uint3 groupCount{ 1, 1, 1 };
};

} // namespace

std::unique_ptr<FilamentApp2> createSampleApp(SampleConfig config,
        filament::app::DisplayManager* dm, filament::app::AssetLoader* loader) {
    auto app = std::make_shared<App>();
    app->config = config;
    auto setup = [app](Engine* engine, View* view, Scene* scene) {
        app->texture = Texture::Builder()
                               .width(TEXTURE_SIZE)
                               .height(TEXTURE_SIZE)
                               .levels(1)
                               .sampler(Texture::Sampler::SAMPLER_2D)
                               .format(Texture::InternalFormat::RGBA8)
                               .usage(Texture::Usage::SAMPLEABLE | Texture::Usage::STORAGE)
                               .build(*engine);

        app->computeMaterial =
                Material::Builder()
                        .package(RESOURCES_COMPUTETEXTURE_DATA, RESOURCES_COMPUTETEXTURE_SIZE)
                        .groupSize({ 8, 8, 1 })
                        .build(*engine);
        math::uint3 const groupSize = app->computeMaterial->getGroupSize();
        app->groupCount = { (TEXTURE_SIZE + groupSize.x - 1) / groupSize.x,
            (TEXTURE_SIZE + groupSize.y - 1) / groupSize.y, 1 };
        app->computeInstance = app->computeMaterial->createInstance();
        app->computeInstance->setParameter("outputTexture", app->texture);

        app->displayMaterial = Material::Builder()
                                       .package(RESOURCES_PROCEDURALTEXTUREQUAD_DATA,
                                               RESOURCES_PROCEDURALTEXTUREQUAD_SIZE)
                                       .build(*engine);
        app->displayInstance = app->displayMaterial->createInstance();
        app->displayInstance->setParameter("albedo", app->texture, TextureSampler{});

        app->vertexBuffer = VertexBuffer::Builder()
                                    .vertexCount(6)
                                    .bufferCount(1)
                                    .attribute(VertexAttribute::POSITION, 0,
                                            VertexBuffer::AttributeType::FLOAT3)
                                    .build(*engine);
        app->vertexBuffer->setBufferAt(*engine, 0,
                VertexBuffer::BufferDescriptor(QUAD_VERTICES, sizeof(QUAD_VERTICES)));
        app->renderable = EntityManager::get().create();
        RenderableManager::Builder(1)
                .boundingBox({ { -0.5f, -0.5f, -0.01f }, { 0.5f, 0.5f, 0.01f } })
                .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, app->vertexBuffer)
                .material(0, app->displayInstance)
                .culling(false)
                .receiveShadows(false)
                .castShadows(false)
                .build(*engine, app->renderable);
        scene->addEntity(app->renderable);

        app->skybox = Skybox::Builder().color({ 0.1f, 0.125f, 0.25f, 1.0f }).build(*engine);
        scene->setSkybox(app->skybox);
        view->setPostProcessingEnabled(false);

        app->cameraEntity = EntityManager::get().create();
        app->camera = engine->createCamera(app->cameraEntity);
        view->setCamera(app->camera);
    };

    auto cleanup = [app](Engine* engine, View*, Scene*) {
        engine->destroy(app->skybox);
        engine->destroy(app->renderable);
        engine->destroy(app->displayInstance);
        engine->destroy(app->displayMaterial);
        engine->destroy(app->computeInstance);
        engine->destroy(app->computeMaterial);
        engine->destroy(app->texture);
        engine->destroy(app->vertexBuffer);
        engine->destroyCameraComponent(app->cameraEntity);
        EntityManager::get().destroy(app->cameraEntity);
    };

    auto fApp = samples::getBuilder(config, dm, loader)
                        .setup(setup)
                        .cleanup(cleanup)
                        .animation([app](Engine* engine, View* view, double time) {
                            float const aspect = float(view->getViewport().width) /
                                                 float(view->getViewport().height);
                            app->camera->setProjection(Camera::Projection::ORTHO, -aspect, aspect,
                                    -1.0f, 1.0f, 0.0f, 1.0f);
                            app->computeInstance->setParameter("time", float(time));
                            engine->dispatch(app->computeInstance, app->groupCount);
                        })
                        .build();
    app->filamentApp = fApp.get();
    return fApp;
}

samples::SampleParameters createAppParameters() { return {}; }

#ifndef __ANDROID__
int main(int argc, char** argv) {
    SampleConfig config;
    config.title = "compute_texture";
    config.featureLevel = backend::FeatureLevel::FEATURE_LEVEL_2;
    samples::handleCommandLineArguments(argc, argv, &config,
            { .parameters = createAppParameters() });
    auto dm = samples::getDisplayManager(config);
    auto loader = samples::getAssetLoader(config);
    auto app = createSampleApp(config, dm.get(), loader.get());
    return samples::runApp(std::move(app), std::move(dm), std::move(loader));
}
#endif
