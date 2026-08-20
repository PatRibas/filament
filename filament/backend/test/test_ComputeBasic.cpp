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

#include "ComputeTest.h"

#include "builtinResource.h"

#include <backend/DriverEnums.h>
#include <backend/Program.h>

#include <utils/FixedCapacityVector.h>

#include <GlslangToSpv.h>
#include <glslang/Public/ShaderLang.h>

#include <vector>

using namespace filament;
using namespace filament::backend;

namespace {

DescriptorSetLayout createStorageBufferLayout() {
    DescriptorSetLayout layout;
    layout.descriptors = utils::FixedCapacityVector<DescriptorSetLayoutDescriptor>::with_capacity(2);
    layout.descriptors.push_back({
        .type = DescriptorType::SHADER_STORAGE_BUFFER,
        .stageFlags = ShaderStageFlags::COMPUTE,
        .binding = 0,
    });
    layout.descriptors.push_back({
        .type = DescriptorType::SHADER_STORAGE_BUFFER,
        .stageFlags = ShaderStageFlags::COMPUTE,
        .binding = 1,
    });
    return layout;
}

DescriptorSetLayout createStorageImageLayout() {
    DescriptorSetLayout layout;
    layout.descriptors = utils::FixedCapacityVector<DescriptorSetLayoutDescriptor>::with_capacity(1);
    layout.descriptors.push_back({
        .type = DescriptorType::STORAGE_IMAGE_2D_FLOAT,
        .stageFlags = ShaderStageFlags::COMPUTE,
        .binding = 0,
    });
    return layout;
}

Program createComputeProgram(const char* shaderSource) {
    glslang::TProgram glslangProgram;
    glslang::TShader glslangShader(EShLangCompute);
    glslangShader.setStrings(&shaderSource, 1);
    glslangShader.setAutoMapBindings(false);

    EShMessages const messages = EShMessages(EShMsgVulkanRules | EShMsgSpvRules);
    if (!glslangShader.parse(&DefaultTBuiltInResource, 450, false, messages)) {
        ADD_FAILURE() << glslangShader.getInfoLog();
        return {};
    }
    glslangProgram.addShader(&glslangShader);
    if (!glslangProgram.link(messages)) {
        ADD_FAILURE() << glslangShader.getInfoLog();
        return {};
    }

    std::vector<uint32_t> spirv;
    glslang::GlslangToSpv(*glslangProgram.getIntermediate(EShLangCompute), spirv);

    Program program;
    program.shaderLanguage(ShaderLanguage::SPIRV);
    program.shader(ShaderStage::COMPUTE, spirv.data(), spirv.size() * sizeof(uint32_t));
    return program;
}

} // anonymous namespace

TEST_F(ComputeTest, dispatchesComputeProgram) {
    auto& driver = getDriverApi();
    ProgramHandle const program = driver.createProgram(createComputeProgram(R"(
#version 450
layout(local_size_x = 16) in;
void main() {}
)"));

    driver.dispatchCompute(program, { 1, 1, 1 });
    driver.destroyProgram(program);
    driver.finish();

    executeCommands();
}

TEST_F(ComputeTest, bindsStorageBuffersForCompute) {
    auto& driver = getDriverApi();
    constexpr uint32_t GROUP_SIZE = 16;
    constexpr uint32_t GROUP_COUNT = 32;
    constexpr uint32_t BUFFER_SIZE = GROUP_SIZE * GROUP_COUNT * sizeof(float);

    std::vector<float> input(GROUP_SIZE * GROUP_COUNT, 1.0f);
    BufferObjectHandle const output = driver.createBufferObject(BUFFER_SIZE,
            BufferObjectBinding::SHADER_STORAGE, BufferUsage::STATIC);
    BufferObjectHandle const inputBuffer = driver.createBufferObject(BUFFER_SIZE,
            BufferObjectBinding::SHADER_STORAGE, BufferUsage::STATIC);
    driver.updateBufferObject(inputBuffer, { input.data(), BUFFER_SIZE }, 0);

    DescriptorSetLayoutHandle const layout = driver.createDescriptorSetLayout(
            createStorageBufferLayout());
    DescriptorSetHandle const descriptorSet = driver.createDescriptorSet(layout);
    driver.updateDescriptorSetBuffer(descriptorSet, 0, output, 0, BUFFER_SIZE);
    driver.updateDescriptorSetBuffer(descriptorSet, 1, inputBuffer, 0, BUFFER_SIZE);
    driver.bindDescriptorSet(descriptorSet, 0, {});

    Program program = createComputeProgram(R"(
#version 450
layout(local_size_x = 16) in;
layout(set = 0, binding = 0, std430) writeonly buffer Output { float elements[]; } outputData;
layout(set = 0, binding = 1, std430) readonly buffer Input { float elements[]; } inputData;
void main() {
    uint index = gl_GlobalInvocationID.x;
    outputData.elements[index] = inputData.elements[index];
}
)");
    program.descriptorLayout(0, createStorageBufferLayout());
    ProgramHandle const computeProgram = driver.createProgram(std::move(program));

    driver.dispatchCompute(computeProgram, { GROUP_COUNT, 1, 1 });
    driver.destroyProgram(computeProgram);
    driver.destroyDescriptorSet(descriptorSet);
    driver.destroyDescriptorSetLayout(layout);
    driver.destroyBufferObject(inputBuffer);
    driver.destroyBufferObject(output);
    driver.finish();

    executeCommands();
}

TEST_F(ComputeTest, writesStorageImageFromCompute) {
    auto& driver = getDriverApi();
    constexpr uint32_t TEXTURE_SIZE = 8;

    TextureHandle const texture = driver.createTexture(SamplerType::SAMPLER_2D, 1,
            TextureFormat::RGBA8, 1, TEXTURE_SIZE, TEXTURE_SIZE, 1,
            TextureUsage::STORAGE | TextureUsage::SAMPLEABLE);
    DescriptorSetLayoutHandle const layout = driver.createDescriptorSetLayout(
            createStorageImageLayout());
    DescriptorSetHandle const descriptorSet = driver.createDescriptorSet(layout);
    driver.updateDescriptorSetTexture(descriptorSet, 0, texture, {});
    driver.bindDescriptorSet(descriptorSet, 0, {});

    Program program = createComputeProgram(R"(
#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0, rgba8) uniform writeonly image2D outputImage;
void main() {
    imageStore(outputImage, ivec2(gl_GlobalInvocationID.xy), vec4(1.0, 0.0, 0.0, 1.0));
}
)");
    program.descriptorLayout(0, createStorageImageLayout());
    ProgramHandle const computeProgram = driver.createProgram(std::move(program));

    driver.dispatchCompute(computeProgram, { 1, 1, 1 });
    driver.destroyProgram(computeProgram);
    driver.destroyDescriptorSet(descriptorSet);
    driver.destroyDescriptorSetLayout(layout);
    driver.destroyTexture(texture);
    driver.finish();

    executeCommands();
}
