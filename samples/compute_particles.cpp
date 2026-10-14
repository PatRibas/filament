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

#include <filament/BufferObject.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>

#include <utils/EntityManager.h>

#include <math/mat3.h>
#include <math/mat4.h>
#include <math/norm.h>
#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <samples/SampleConfig.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace filament;
using namespace filament::math;
using utils::Entity;
using utils::EntityManager;

namespace {

constexpr int DEFAULT_PARTICLE_COUNT = 10485760;
constexpr int DEFAULT_GROUP_SIZE = 64;

static const short4 QUAD_TBN = packSnorm16(
        mat3f::packTangentFrame(
                mat3f{
                    float3{ 1.0f, 0.0f, 0.0f },
                    float3{ 0.0f, 1.0f, 0.0f },
                    float3{ 0.0f, 0.0f, 1.0f }
                }
        ).xyzw);

struct QuadVertex {
    float3 position;
    short4 tangents;
};

static const QuadVertex QUAD_VERTICES[4] = {
    { { -0.5f, -0.5f, 0.0f }, QUAD_TBN },
    { {  0.5f, -0.5f, 0.0f }, QUAD_TBN },
    { { -0.5f,  0.5f, 0.0f }, QUAD_TBN },
    { {  0.5f,  0.5f, 0.0f }, QUAD_TBN },
};

static const uint16_t QUAD_INDICES[6] = {
    0, 1, 2,
    2, 1, 3
};

struct ParticleInitialData {
    std::vector<float4> positions;
    std::vector<float4> velocities;
    std::vector<float4> colors;
};

struct App {
    FilamentApp2* filamentApp = nullptr;
    SampleConfig config;
    Entity renderable;
    Entity light;
    Material* computeMaterial = nullptr;
    MaterialInstance* computeInstance = nullptr;
    Material* renderMaterial = nullptr;
    MaterialInstance* renderInstance = nullptr;
    BufferObject* positionsBuffer = nullptr;
    BufferObject* velocitiesBuffer = nullptr;
    BufferObject* colorsBuffer = nullptr;
    VertexBuffer* vertexBuffer = nullptr;
    IndexBuffer* indexBuffer = nullptr;
    std::unique_ptr<ParticleInitialData> initialData;
    uint32_t groupCount = 0;
    double lastTime = 0.016;
};

std::unique_ptr<ParticleInitialData> createInitialParticles(uint32_t count) {
    auto data = std::make_unique<ParticleInitialData>();
    data->positions.resize(count);
    data->velocities.resize(count);
    data->colors.resize(count);

    uint32_t seed = 12345u;
    auto lcg = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return float(seed) / 4294967295.0f;
    };

    for (uint32_t i = 0; i < count; i++) {
        float const px = (lcg() * 2.0f - 1.0f) * 1.2f;
        float const py = (lcg() * 2.0f - 1.0f) * 1.2f;
        float const pz = (lcg() * 2.0f - 1.0f) * 1.2f;
        data->positions[i] = { px, py, pz, 1.0f };

        float vx = lcg() * 2.0f - 1.0f;
        float vy = lcg() * 2.0f - 1.0f;
        float vz = lcg() * 2.0f - 1.0f;
        float const speed = 0.4f + lcg() * 0.8f;
        float const len = std::sqrt(vx * vx + vy * vy + vz * vz);
        if (len > 1e-4f) {
            vx = (vx / len) * speed;
            vy = (vy / len) * speed;
            vz = (vz / len) * speed;
        }
        data->velocities[i] = { vx, vy, vz, 0.0f };

        float const cr = std::clamp(px * 0.5f + 0.5f, 0.05f, 1.0f);
        float const cg = std::clamp(py * 0.5f + 0.5f, 0.05f, 1.0f);
        float const cb = std::clamp(pz * 0.5f + 0.5f, 0.05f, 1.0f);
        data->colors[i] = { cr, cg, cb, 1.0f };
    }
    return data;
}

} // namespace

std::unique_ptr<FilamentApp2> createSampleApp(SampleConfig config,
        filament::app::DisplayManager* dm, filament::app::AssetLoader* loader) {
    if (config.iblDirectory.empty()) {
        config.iblDirectory = "assets/ibl/lightroom_14b";
    }
    auto app = std::make_shared<App>();
    app->config = config;

    uint32_t const particleCount =
            static_cast<uint32_t>(config.getInt("count", DEFAULT_PARTICLE_COUNT));
    uint32_t const groupSizeX =
            static_cast<uint32_t>(config.getInt("group-size", DEFAULT_GROUP_SIZE));

    auto setup = [app, particleCount, groupSizeX](Engine* engine, View*, Scene* scene) {
        app->initialData = createInitialParticles(particleCount);
        size_t const bufferSize = particleCount * sizeof(float4);

        app->positionsBuffer = BufferObject::Builder()
                .size(bufferSize)
                .bindingType(BufferObject::BindingType::SHADER_STORAGE)
                .name("Positions")
                .build(*engine);
        app->positionsBuffer->setBuffer(*engine,
                BufferObject::BufferDescriptor(app->initialData->positions.data(), bufferSize, nullptr));

        app->velocitiesBuffer = BufferObject::Builder()
                .size(bufferSize)
                .bindingType(BufferObject::BindingType::SHADER_STORAGE)
                .name("Velocities")
                .build(*engine);
        app->velocitiesBuffer->setBuffer(*engine,
                BufferObject::BufferDescriptor(app->initialData->velocities.data(), bufferSize, nullptr));

        app->colorsBuffer = BufferObject::Builder()
                .size(bufferSize)
                .bindingType(BufferObject::BindingType::SHADER_STORAGE)
                .name("Colors")
                .build(*engine);
        app->colorsBuffer->setBuffer(*engine,
                BufferObject::BufferDescriptor(app->initialData->colors.data(), bufferSize, nullptr));

        app->computeMaterial = Material::Builder()
                .package(RESOURCES_COMPUTEPARTICLES_DATA, RESOURCES_COMPUTEPARTICLES_SIZE)
                .groupSize({ groupSizeX, 1, 1 })
                .build(*engine);

        math::uint3 const groupSize = app->computeMaterial->getGroupSize();
        app->groupCount = (particleCount + groupSize.x - 1) / groupSize.x;

        app->computeInstance = app->computeMaterial->createInstance();
        app->computeInstance->setParameter("Positions", app->positionsBuffer);
        app->computeInstance->setParameter("Velocities", app->velocitiesBuffer);
        app->computeInstance->setParameter("Colors", app->colorsBuffer);
        app->computeInstance->setParameter("count", particleCount);

        app->renderMaterial = Material::Builder()
                .package(RESOURCES_PARTICLERENDER_DATA, RESOURCES_PARTICLERENDER_SIZE)
                .build(*engine);
        app->renderInstance = app->renderMaterial->createInstance();
        app->renderInstance->setParameter("Positions", app->positionsBuffer);
        app->renderInstance->setParameter("Colors", app->colorsBuffer);

        app->vertexBuffer = VertexBuffer::Builder()
                .vertexCount(4)
                .bufferCount(1)
                .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3, 0, sizeof(QuadVertex))
                .attribute(VertexAttribute::TANGENTS, 0, VertexBuffer::AttributeType::SHORT4, offsetof(QuadVertex, tangents), sizeof(QuadVertex))
                .normalized(VertexAttribute::TANGENTS)
                .build(*engine);
        app->vertexBuffer->setBufferAt(*engine, 0,
                VertexBuffer::BufferDescriptor(QUAD_VERTICES, sizeof(QUAD_VERTICES), nullptr));

        app->indexBuffer = IndexBuffer::Builder()
                .indexCount(6)
                .bufferType(IndexBuffer::IndexType::USHORT)
                .build(*engine);
        app->indexBuffer->setBuffer(*engine,
                IndexBuffer::BufferDescriptor(QUAD_INDICES, sizeof(QUAD_INDICES), nullptr));

        app->renderable = EntityManager::get().create();
        RenderableManager::Builder(1)
                .boundingBox({{ -1.6f, -1.6f, -1.6f }, { 1.6f, 1.6f, 1.6f }})
                .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, app->vertexBuffer, app->indexBuffer)
                .material(0, app->renderInstance)
                .instances(particleCount)
                .culling(false)
                .receiveShadows(false)
                .castShadows(false)
                .build(*engine, app->renderable);

        auto& tcm = engine->getTransformManager();
        auto ti = tcm.getInstance(app->renderable);
        tcm.setTransform(ti, mat4f::translation(float3{ 0.0f, 0.0f, -4.0f }));
        scene->addEntity(app->renderable);

        app->light = EntityManager::get().create();
        LightManager::Builder(LightManager::Type::SUN)
                .color(Color::toLinear<ACCURATE>(sRGBColor(0.98f, 0.92f, 0.89f)))
                .intensity(110000)
                .direction({ 0.7f, -1.0f, -0.8f })
                .sunAngularRadius(1.9f)
                .castShadows(false)
                .build(*engine, app->light);
        scene->addEntity(app->light);
    };

    auto cleanup = [app](Engine* engine, View*, Scene*) {
        engine->destroy(app->renderable);
        engine->destroy(app->light);

        engine->destroy(app->renderInstance);
        engine->destroy(app->renderMaterial);

        engine->destroy(app->computeInstance);
        engine->destroy(app->computeMaterial);

        engine->destroy(app->positionsBuffer);
        engine->destroy(app->velocitiesBuffer);
        engine->destroy(app->colorsBuffer);
        engine->destroy(app->vertexBuffer);
        engine->destroy(app->indexBuffer);
    };

    auto fApp = samples::getBuilder(config, dm, loader)
                        .setup(setup)
                        .cleanup(cleanup)
                        .animation([app](Engine* engine, View*, double time) {
                            float dt = float(time - app->lastTime);
                            app->lastTime = time;
                            dt = std::clamp(dt, 0.001f, 0.05f);

                            app->computeInstance->setParameter("deltaTime", dt);
                            engine->dispatch(app->computeInstance, { app->groupCount, 1, 1 });
                        })
                        .build();
    app->filamentApp = fApp.get();
    return fApp;
}

samples::SampleParameters createAppParameters() {
    return {
        samples::Parameter::makeInt("count", 'C', "Number of particles to render",
                DEFAULT_PARTICLE_COUNT, 1),
        samples::Parameter::makeInt("group-size", 'G', "Compute workgroup size X",
                DEFAULT_GROUP_SIZE, 1),
    };
}

#ifndef __ANDROID__
int main(int argc, char** argv) {
    SampleConfig config;
    config.title = "compute_particles";
    config.iblDirectory = "assets/ibl/lightroom_14b";
    samples::handleCommandLineArguments(argc, argv, &config,
            { .parameters = createAppParameters() });
    auto dm = samples::getDisplayManager(config);
    auto loader = samples::getAssetLoader(config);
    auto app = createSampleApp(config, dm.get(), loader.get());
    return samples::runApp(std::move(app), std::move(dm), std::move(loader));
}
#endif
