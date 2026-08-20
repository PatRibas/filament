/*
 * Copyright (C) 2017 The Android Open Source Project
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
#include "MaterialInterfaceBlockChunk.h"

#include <private/filament/BufferInterfaceBlock.h>
#include <private/filament/ConstantInfo.h>
#include <private/filament/DescriptorSets.h>
#include <private/filament/EngineEnums.h>
#include <private/filament/PushConstantInfo.h>
#include <private/filament/SamplerInterfaceBlock.h>
#include <private/filament/SubpassInfo.h>

#include <filament/MaterialChunkType.h>

#include <backend/DriverEnums.h>

#include <utils/compiler.h>
#include <utils/CString.h>
#include <utils/debug.h>
#include <utils/FixedCapacityVector.h>

#include <utility>

#include <stdint.h>

using namespace filament;

namespace filamat {

MaterialUniformInterfaceBlockChunk::MaterialUniformInterfaceBlockChunk(
        BufferInterfaceBlock const& uib) :
        Chunk(MaterialUib),
        mUib(uib) {
}

void MaterialUniformInterfaceBlockChunk::flatten(Flattener& f) {
    f.writeString(mUib.getName());
    auto uibFields = mUib.getFieldInfoList();
    f.writeUint64(uibFields.size());
    for (auto uInfo: uibFields) {
        f.writeString(uInfo.name.c_str());
        f.writeUint64(uInfo.size);
        f.writeUint8(static_cast<uint8_t>(uInfo.type));
        f.writeUint8(static_cast<uint8_t>(uInfo.precision));
        f.writeUint8(static_cast<uint8_t>(uInfo.associatedSampler));
    }
}

// ------------------------------------------------------------------------------------------------

MaterialSamplerInterfaceBlockChunk::MaterialSamplerInterfaceBlockChunk(
        SamplerInterfaceBlock const& sib) :
        Chunk(MaterialSib),
        mSib(sib) {
}

void MaterialSamplerInterfaceBlockChunk::flatten(Flattener& f) {
    f.writeString(mSib.getName().c_str());
    auto sibFields = mSib.getSamplerInfoList();
    f.writeUint64(sibFields.size());
    for (auto sInfo: sibFields) {
        f.writeString(sInfo.name.c_str());
        f.writeUint8(static_cast<uint8_t>(sInfo.binding));
        f.writeUint8(static_cast<uint8_t>(sInfo.type));
        f.writeUint8(static_cast<uint8_t>(sInfo.format));
        f.writeUint8(static_cast<uint8_t>(sInfo.precision));
        f.writeBool(sInfo.filterable);
        f.writeBool(sInfo.multisample);
        f.writeString(sInfo.transformName.c_str_safe());
    }
}

// ------------------------------------------------------------------------------------------------

MaterialSubpassInterfaceBlockChunk::MaterialSubpassInterfaceBlockChunk(SubpassInfo const& subpass) :
        Chunk(MaterialSubpass),
        mSubpass(subpass) {
}

void MaterialSubpassInterfaceBlockChunk::flatten(Flattener& f) {
    f.writeString(mSubpass.block.c_str());
    f.writeUint64(mSubpass.isValid ? 1 : 0);   // only ever a single subpass for now
    if (mSubpass.isValid) {
        f.writeString(mSubpass.name.c_str());
        f.writeUint8(static_cast<uint8_t>(mSubpass.type));
        f.writeUint8(static_cast<uint8_t>(mSubpass.format));
        f.writeUint8(static_cast<uint8_t>(mSubpass.precision));
        f.writeUint8(static_cast<uint8_t>(mSubpass.attachmentIndex));
        f.writeUint8(static_cast<uint8_t>(mSubpass.binding));
    }
}

// ------------------------------------------------------------------------------------------------

MaterialConstantParametersChunk::MaterialConstantParametersChunk(
        FixedCapacityVector<MaterialConstant> constants)
    : Chunk(MaterialConstants), mConstants(std::move(constants)) {}

void MaterialConstantParametersChunk::flatten(Flattener& f) {
    f.writeUint64(mConstants.size());
    for (const auto& constant : mConstants) {
        f.writeString(constant.name.c_str());
        f.writeUint8(static_cast<uint8_t>(constant.type));
        f.writeUint32(static_cast<uint32_t>(constant.defaultValue.i));
    }
}

// ------------------------------------------------------------------------------------------------

MaterialPushConstantParametersChunk::MaterialPushConstantParametersChunk(
        CString const& structVarName, FixedCapacityVector<MaterialPushConstant> constants)
    : Chunk(MaterialPushConstants),
      mStructVarName(structVarName),
      mConstants(std::move(constants)) {}

void MaterialPushConstantParametersChunk::flatten(Flattener& f) {
    f.writeString(mStructVarName.c_str());
    f.writeUint64(mConstants.size());
    for (const auto& constant: mConstants) {
        f.writeString(constant.name.c_str());
        f.writeUint8(static_cast<uint8_t>(constant.type));
        f.writeUint8(static_cast<uint8_t>(constant.stage));
    }
}

// ------------------------------------------------------------------------------------------------

MaterialBindingUniformInfoChunk::MaterialBindingUniformInfoChunk(Container list) noexcept
        : Chunk(MaterialBindingUniformInfo),
          mBindingUniformInfo(std::move(list)) {
}

void MaterialBindingUniformInfoChunk::flatten(Flattener& f) {
    f.writeUint8(mBindingUniformInfo.size());
    for (auto const& [index, name, uniforms] : mBindingUniformInfo) {
        f.writeUint8(uint8_t(index));
        f.writeString({ name.data(), name.size() });
        f.writeUint8(uint8_t(uniforms.size()));
        for (auto const& uniform: uniforms) {
            f.writeString({ uniform.name.data(), uniform.name.size() });
            f.writeUint16(uniform.offset);
            f.writeUint8(uniform.size);
            f.writeUint8(uint8_t(uniform.type));
        }
    }
}

// ------------------------------------------------------------------------------------------------

MaterialAttributesInfoChunk::MaterialAttributesInfoChunk(Container list) noexcept
        : Chunk(MaterialAttributeInfo),
          mAttributeInfo(std::move(list))
{
}

void MaterialAttributesInfoChunk::flatten(Flattener& f) {
    f.writeUint8(mAttributeInfo.size());
    for (auto const& [attribute, location]: mAttributeInfo) {
        f.writeString({ attribute.data(), attribute.size() });
        f.writeUint8(location);
    }
}

// ------------------------------------------------------------------------------------------------

MaterialDescriptorBindingsChuck::MaterialDescriptorBindingsChuck(Container const& bindings) noexcept
        : Chunk(MaterialDescriptorBindingsInfo),
          mBindings(bindings) {
}

void MaterialDescriptorBindingsChuck::flatten(Flattener& f) {
    assert_invariant(sizeof(backend::descriptor_set_t) == sizeof(uint8_t));
    assert_invariant(sizeof(backend::descriptor_binding_t) == sizeof(uint8_t));

    using namespace backend;

    f.writeUint8(mBindings.size());
    for (auto const& entry : mBindings) {
        f.writeString({ entry.name.data(), entry.name.size() });
        f.writeUint8(uint8_t(entry.type));
        f.writeUint8(entry.binding);
    }
}

// ------------------------------------------------------------------------------------------------

MaterialDescriptorSetLayoutChunk::MaterialDescriptorSetLayoutChunk(Container const& layout) noexcept
        : Chunk(MaterialDescriptorSetLayoutInfo),
          mLayout(layout) {
}

void MaterialDescriptorSetLayoutChunk::flatten(Flattener& f) {
    assert_invariant(sizeof(backend::descriptor_set_t) == sizeof(uint8_t));
    assert_invariant(sizeof(backend::descriptor_binding_t) == sizeof(uint8_t));

    using namespace backend;

    f.writeUint8(mLayout.descriptors.size());
    for (auto const& descriptor : mLayout.descriptors) {
        f.writeUint8(uint8_t(descriptor.type));
        f.writeUint8(uint8_t(descriptor.stageFlags));
        f.writeUint8(descriptor.binding);
        f.writeUint8(uint8_t(descriptor.flags));
        f.writeUint16(descriptor.count);
    }
}

} // namespace filamat
