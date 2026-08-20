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

#include <filamentapp/Config.h>
#include <filamentapp/FilamentApp.h>

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

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

using namespace filament;
using namespace filament::math;
using utils::Entity;
using utils::EntityManager;

namespace {

constexpr uint32_t PARTICLE_COUNT = 10485760;
constexpr uint32_t GROUP_COUNT = PARTICLE_COUNT / 64;

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

struct ParticleBufferData {
    std::array<float4, PARTICLE_COUNT> positions;
    std::array<float4, PARTICLE_COUNT> velocities;
    std::array<float4, PARTICLE_COUNT> colors;
};

struct App {
    Entity renderable;
    Entity light;
    Material* computeMaterial = nullptr;
    MaterialInstance* computeInstance = nullptr;
    Material* renderMaterial = nullptr;
    MaterialInstance* renderInstance = nullptr;
    BufferObject* particleBuffer = nullptr;
    VertexBuffer* vertexBuffer = nullptr;
    IndexBuffer* indexBuffer = nullptr;
};

std::unique_ptr<ParticleBufferData> createInitialParticles() {
    auto data = std::make_unique<ParticleBufferData>();
    uint32_t seed = 12345u;
    auto lcg = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return float(seed) / 4294967295.0f;
    };

    for (uint32_t i = 0; i < PARTICLE_COUNT; i++) {
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

int main(int argc, char** argv) {
    Config config;
    config.title = "compute_particles";
    config.backend = samples::parseArgumentsForBackend(argc, argv);
    config.iblDirectory = FilamentApp::getRootAssetsPath() + "assets/ibl/lightroom_14b";

    App app;
    auto setup = [&app](Engine* engine, View* view, Scene* scene) {
        static auto const initialData = createInitialParticles();

        app.particleBuffer = BufferObject::Builder()
                .size(sizeof(ParticleBufferData))
                .bindingType(BufferObject::BindingType::SHADER_STORAGE)
                .name("ParticleBuffer")
                .build(*engine);
        app.particleBuffer->setBuffer(*engine,
                BufferObject::BufferDescriptor(initialData.get(), sizeof(ParticleBufferData), nullptr));

        app.computeMaterial = Material::Builder()
                .package(RESOURCES_COMPUTEPARTICLES_DATA, RESOURCES_COMPUTEPARTICLES_SIZE)
                .build(*engine);
        app.computeInstance = app.computeMaterial->createInstance();
        app.computeInstance->setParameter("Particles", app.particleBuffer);

        app.renderMaterial = Material::Builder()
                .package(RESOURCES_PARTICLERENDER_DATA, RESOURCES_PARTICLERENDER_SIZE)
                .build(*engine);
        app.renderInstance = app.renderMaterial->createInstance();
        app.renderInstance->setParameter("Particles", app.particleBuffer);

        app.vertexBuffer = VertexBuffer::Builder()
                .vertexCount(4)
                .bufferCount(1)
                .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3, 0, sizeof(QuadVertex))
                .attribute(VertexAttribute::TANGENTS, 0, VertexBuffer::AttributeType::SHORT4, offsetof(QuadVertex, tangents), sizeof(QuadVertex))
                .normalized(VertexAttribute::TANGENTS)
                .build(*engine);
        app.vertexBuffer->setBufferAt(*engine, 0,
                VertexBuffer::BufferDescriptor(QUAD_VERTICES, sizeof(QUAD_VERTICES), nullptr));

        app.indexBuffer = IndexBuffer::Builder()
                .indexCount(6)
                .bufferType(IndexBuffer::IndexType::USHORT)
                .build(*engine);
        app.indexBuffer->setBuffer(*engine,
                IndexBuffer::BufferDescriptor(QUAD_INDICES, sizeof(QUAD_INDICES), nullptr));

        app.renderable = EntityManager::get().create();
        RenderableManager::Builder(1)
                .boundingBox({{ -1.6f, -1.6f, -1.6f }, { 1.6f, 1.6f, 1.6f }})
                .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, app.vertexBuffer, app.indexBuffer)
                .material(0, app.renderInstance)
                .instances(PARTICLE_COUNT)
                .culling(false)
                .receiveShadows(false)
                .castShadows(false)
                .build(*engine, app.renderable);

        auto& tcm = engine->getTransformManager();
        auto ti = tcm.getInstance(app.renderable);
        tcm.setTransform(ti, mat4f::translation(float3{ 0.0f, 0.0f, -4.0f }));
        scene->addEntity(app.renderable);

        app.light = EntityManager::get().create();
        LightManager::Builder(LightManager::Type::SUN)
                .color(Color::toLinear<ACCURATE>(sRGBColor(0.98f, 0.92f, 0.89f)))
                .intensity(110000)
                .direction({ 0.7f, -1.0f, -0.8f })
                .sunAngularRadius(1.9f)
                .castShadows(false)
                .build(*engine, app.light);
        scene->addEntity(app.light);
    };

    auto cleanup = [&app](Engine* engine, View*, Scene*) {
        engine->destroy(app.renderable);
        engine->destroy(app.light);

        engine->destroy(app.renderInstance);
        engine->destroy(app.renderMaterial);

        engine->destroy(app.computeInstance);
        engine->destroy(app.computeMaterial);

        engine->destroy(app.particleBuffer);
        engine->destroy(app.vertexBuffer);
        engine->destroy(app.indexBuffer);
    };

    static double lastTime = 0.016f;
    FilamentApp::get().animate([&app](Engine* engine, View*, double time) {
        float dt = float(time - lastTime);
        lastTime = time;
        dt = std::clamp(dt, 0.001f, 0.05f);

        app.computeInstance->setParameter("deltaTime", dt);
        engine->dispatch(app.computeInstance, { GROUP_COUNT, 1, 1 });
    });

    FilamentApp::get().run(config, setup, cleanup);
    return 0;
}


