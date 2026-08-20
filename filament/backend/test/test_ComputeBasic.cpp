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
