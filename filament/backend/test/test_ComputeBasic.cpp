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

#include "builtinResource.h"
#include "ComputeTest.h"

#include "../src/GLSLPostProcessor.h"

#include <backend/BufferDescriptor.h>
#include <backend/DriverEnums.h>
#include <backend/PixelBufferDescriptor.h>
#include <backend/Program.h>

#include <utils/CString.h>
#include <utils/FixedCapacityVector.h>

#include <glslang/Public/ShaderLang.h>
#include <GlslangToSpv.h>
#include <spirv_glsl.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace filament;
using namespace filament::backend;

namespace {

Program createComputeProgram(const char* shaderSource, Backend backend = Backend::VULKAN,
        bool isMobile = false, const DescriptorSetLayout& set0Layout = {}) {
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

    if (backend == Backend::METAL) {
        filamat::DescriptorSets descriptors;
        descriptors[0].reserve(set0Layout.descriptors.size());
        for (auto const& d : set0Layout.descriptors) {
            std::string const dummyName = "Resource" + std::to_string(d.binding);
            descriptors[0].push_back({ utils::CString(dummyName.c_str()), d, std::nullopt });
        }
        std::string msl;
        ShaderModel const sm = isMobile ? ShaderModel::MOBILE : ShaderModel::DESKTOP;
        filamat::GLSLPostProcessor::spirvToMsl(
                &spirv, &msl, ShaderStage::COMPUTE, sm, false, descriptors, nullptr);
        Program program;
        program.shaderLanguage(ShaderLanguage::MSL);
        program.shader(ShaderStage::COMPUTE, msl.data(), msl.size() + 1);
        return program;
    }

    if (backend == Backend::OPENGL) {
        spirv_cross::CompilerGLSL glslCompiler(spirv);
        spirv_cross::CompilerGLSL::Options options;
        options.version = isMobile ? 310 : 430;
        options.es = isMobile;
        options.enable_420pack_extension = !isMobile;
        glslCompiler.set_common_options(options);

        Program::DescriptorBindingsInfo descriptors;
        descriptors.reserve(set0Layout.descriptors.size());
        spirv_cross::ShaderResources const resources = glslCompiler.get_shader_resources();
        for (auto const& d : set0Layout.descriptors) {
            utils::CString name;
            if (d.type == DescriptorType::SHADER_STORAGE_BUFFER) {
                for (auto const& sb : resources.storage_buffers) {
                    if (glslCompiler.get_decoration(sb.id, spv::DecorationBinding) == d.binding) {
                        name = utils::CString(sb.name.c_str());
                        break;
                    }
                }
            } else if (d.type == DescriptorType::STORAGE_IMAGE_2D_FLOAT ||
                       d.type == DescriptorType::STORAGE_IMAGE_2D_INT ||
                       d.type == DescriptorType::STORAGE_IMAGE_2D_UINT) {
                for (auto const& img : resources.storage_images) {
                    if (glslCompiler.get_decoration(img.id, spv::DecorationBinding) == d.binding) {
                        name = utils::CString(img.name.c_str());
                        break;
                    }
                }
            }
            descriptors.push_back({ name, d.type, d.binding });
        }

        std::string const glsl = glslCompiler.compile();
        Program program;
        program.shaderLanguage(ShaderLanguage::ESSL3);
        program.shader(ShaderStage::COMPUTE, glsl.data(), glsl.size() + 1);
        if (!descriptors.empty()) {
            program.descriptorBindings(0, std::move(descriptors));
        }
        return program;
    }

    Program program;
    program.shaderLanguage(ShaderLanguage::SPIRV);
    program.shader(ShaderStage::COMPUTE, spirv.data(), spirv.size() * sizeof(uint32_t));
    return program;
}

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

} // anonymous namespace

TEST_F(ComputeTest, dispatchesComputeProgram) {
    auto& driver = getDriverApi();
    ProgramHandle const program = driver.createProgram(createComputeProgram(R"(
#version 450
layout(local_size_x = 16) in;
void main() {}
)", getBackend(), isMobile()));

    driver.dispatchCompute(program, { 1, 1, 1 });
    driver.destroyProgram(program);
    driver.finish();

    executeCommands();
}

TEST_F(ComputeTest, bindsStorageBuffersForCompute) {
    auto& driver = getDriverApi();
    constexpr uint32_t GROUP_SIZE = 16;
    constexpr uint32_t GROUP_COUNT = 32;
    constexpr uint32_t ELEMENT_COUNT = GROUP_SIZE * GROUP_COUNT;
    constexpr uint32_t BUFFER_SIZE = ELEMENT_COUNT * sizeof(float);

    std::vector<float> input(ELEMENT_COUNT, 1.0f);
    std::vector<float> readback(ELEMENT_COUNT, 0.0f);
    BufferObjectHandle const output = driver.createBufferObject(BUFFER_SIZE,
            BufferObjectBinding::SHADER_STORAGE, BufferUsage::STATIC);
    BufferObjectHandle const inputBuffer = driver.createBufferObject(BUFFER_SIZE,
            BufferObjectBinding::SHADER_STORAGE, BufferUsage::STATIC);
    driver.updateBufferObject(inputBuffer, { input.data(), BUFFER_SIZE }, 0);

    DescriptorSetLayout const set0Layout = createStorageBufferLayout();
    DescriptorSetLayoutHandle const layout =
            driver.createDescriptorSetLayout(createStorageBufferLayout());
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
)", getBackend(), isMobile(), set0Layout);
    program.descriptorLayout(0, createStorageBufferLayout());
    ProgramHandle const computeProgram = driver.createProgram(std::move(program));

    driver.dispatchCompute(computeProgram, { GROUP_COUNT, 1, 1 });
    if (getBackend() == Backend::METAL || getBackend() == Backend::OPENGL) {
        driver.readBufferSubData(output, 0, BUFFER_SIZE, { readback.data(), BUFFER_SIZE });
    }
    driver.destroyProgram(computeProgram);
    driver.destroyDescriptorSet(descriptorSet);
    driver.destroyDescriptorSetLayout(layout);
    driver.destroyBufferObject(inputBuffer);
    driver.destroyBufferObject(output);
    driver.finish();

    executeCommands();

    if (getBackend() == Backend::METAL || getBackend() == Backend::OPENGL) {
        for (uint32_t i = 0; i < ELEMENT_COUNT; i++) {
            EXPECT_FLOAT_EQ(readback[i], 1.0f);
        }
    }
}

TEST_F(ComputeTest, writesStorageImageFromCompute) {
    auto& driver = getDriverApi();
    constexpr uint32_t TEXTURE_SIZE = 8;

    TextureHandle const texture = driver.createTexture(SamplerType::SAMPLER_2D, 1,
            TextureFormat::RGBA8, 1, TEXTURE_SIZE, TEXTURE_SIZE, 1,
            TextureUsage::STORAGE | TextureUsage::SAMPLEABLE | TextureUsage::BLIT_SRC);
    DescriptorSetLayout const set0Layout = createStorageImageLayout();
    DescriptorSetLayoutHandle const layout =
            driver.createDescriptorSetLayout(createStorageImageLayout());
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
)", getBackend(), isMobile(), set0Layout);
    program.descriptorLayout(0, createStorageImageLayout());
    ProgramHandle const computeProgram = driver.createProgram(std::move(program));

    std::vector<uint8_t> pixels(TEXTURE_SIZE * TEXTURE_SIZE * 4, 0);
    driver.dispatchCompute(computeProgram, { 1, 1, 1 });
    if (getBackend() == Backend::METAL || getBackend() == Backend::OPENGL) {
        driver.readTexture(texture, 0, 0, 0, 0, TEXTURE_SIZE, TEXTURE_SIZE,
                PixelBufferDescriptor(pixels.data(), pixels.size(),
                        PixelDataFormat::RGBA, PixelDataType::UBYTE));
    }
    driver.destroyProgram(computeProgram);
    driver.destroyDescriptorSet(descriptorSet);
    driver.destroyDescriptorSetLayout(layout);
    driver.destroyTexture(texture);
    driver.finish();

    executeCommands();

    if (getBackend() == Backend::METAL || getBackend() == Backend::OPENGL) {
        for (size_t i = 0; i < pixels.size(); i += 4) {
            EXPECT_EQ(pixels[i + 0], 255);
            EXPECT_EQ(pixels[i + 1], 0);
            EXPECT_EQ(pixels[i + 2], 0);
            EXPECT_EQ(pixels[i + 3], 255);
        }
    }
}
