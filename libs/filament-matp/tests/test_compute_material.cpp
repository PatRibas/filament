/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <gtest/gtest.h>

#include "TestMaterialParser.h"
#include "utils/JobSystem.h"
#include <filament-matp/MaterialParser.h>

static std::string_view jsonMaterialSourceSimple(R"(
material {
    name: test_compute,
    domain: compute,
    groupSize: [8, 8, 1],
    buffers: [
        {
            name: Result,
            qualifiers: [writeonly],
            fields: [
                { name: value, type: float }
            ]
        }
    ],
    images: [
        { name: outputImage, format: rgba8 }
    ]
}
compute {
    void compute() {
        ivec2 coordinate = ivec2(gl_GlobalInvocationID.xy);
        imageStore(outputImage, coordinate, vec4(1.0));
        result.value = 1.0;
    }
}
)");

TEST(TestParseAndComputeMaterial, JsonMaterialCompilerSimple) {
    matp::MaterialParser parser;
    TestMaterialParser testParser(parser);

    filamat::MaterialBuilder::init();
    filamat::MaterialBuilder builder;

    utils::Status result = testParser.parseMaterial(
            jsonMaterialSourceSimple.data(), jsonMaterialSourceSimple.size(), builder);

    EXPECT_EQ(result.getCode(), utils::StatusCode::OK);

    utils::JobSystem js;
    js.adopt();

    auto package = builder.build(js);

    EXPECT_TRUE(package.isValid());

    js.emancipate();
    filamat::MaterialBuilder::shutdown();
}
