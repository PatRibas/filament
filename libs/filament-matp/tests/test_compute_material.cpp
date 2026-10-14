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
        {
            name: outputImage,
            format: rgba8
            qualifiers: [coherent writeonly],
        },
        {
            name: rwImage,
            format: r32f
        }
    ]
}
compute {
    void compute() {
        ivec2 coordinate = ivec2(gl_GlobalInvocationID.xy);
        vec4 v = imageLoad(rwImage, coordinate);
        imageStore(rwImage, coordinate, v + vec4(1.0));
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

static void expectValidComputeMaterial(std::string_view source) {
    matp::MaterialParser parser;
    TestMaterialParser testParser(parser);

    filamat::MaterialBuilder::init();
    filamat::MaterialBuilder builder;

    utils::Status result = testParser.parseMaterial(source.data(), source.size(), builder);
    EXPECT_EQ(result.getCode(), utils::StatusCode::OK);

    utils::JobSystem js;
    js.adopt();

    auto package = builder.build(js);
    EXPECT_TRUE(package.isValid());

    js.emancipate();
    filamat::MaterialBuilder::shutdown();
}

TEST(TestParseAndComputeMaterial, ImageQualifierDefault) {
    expectValidComputeMaterial(R"(
        material {
            name: test_img_default,
            domain: compute,
            groupSize: [8, 8, 1],
            images: [ { name: img, format: r32f } ]
        }
        compute {
            void compute() {
                ivec2 c = ivec2(gl_GlobalInvocationID.xy);
                imageStore(img, c, imageLoad(img, c) + vec4(1.0));
            }
        }
    )");
}

TEST(TestParseAndComputeMaterial, ImageQualifierCoherent) {
    expectValidComputeMaterial(R"(
        material {
            name: test_img_coherent,
            domain: compute,
            groupSize: [8, 8, 1],
            images: [ { name: img, format: r32f, qualifiers: [coherent] } ]
        }
        compute {
            void compute() {
                ivec2 c = ivec2(gl_GlobalInvocationID.xy);
                imageStore(img, c, imageLoad(img, c) + vec4(1.0));
            }
        }
    )");
}

TEST(TestParseAndComputeMaterial, ImageQualifierWriteonly) {
    expectValidComputeMaterial(R"(
        material {
            name: test_img_writeonly,
            domain: compute,
            groupSize: [8, 8, 1],
            images: [ { name: img, format: rgba8, qualifiers: [writeonly] } ]
        }
        compute {
            void compute() {
                ivec2 c = ivec2(gl_GlobalInvocationID.xy);
                imageStore(img, c, vec4(1.0));
            }
        }
    )");
}

TEST(TestParseAndComputeMaterial, ImageQualifierReadonly) {
    expectValidComputeMaterial(R"(
        material {
            name: test_img_readonly,
            domain: compute,
            groupSize: [8, 8, 1],
            images: [
                { name: inImg, format: rgba8, qualifiers: [readonly] },
                { name: outImg, format: rgba8, qualifiers: [writeonly] }
            ]
        }
        compute {
            void compute() {
                ivec2 c = ivec2(gl_GlobalInvocationID.xy);
                imageStore(outImg, c, imageLoad(inImg, c));
            }
        }
    )");
}

TEST(TestParseAndComputeMaterial, ImageQualifierVolatile) {
    expectValidComputeMaterial(R"(
        material {
            name: test_img_volatile,
            domain: compute,
            groupSize: [8, 8, 1],
            images: [ { name: img, format: r32f, qualifiers: [volatile] } ]
        }
        compute {
            void compute() {
                ivec2 c = ivec2(gl_GlobalInvocationID.xy);
                imageStore(img, c, imageLoad(img, c) + vec4(1.0));
            }
        }
    )");
}

TEST(TestParseAndComputeMaterial, ImageQualifierRestrict) {
    expectValidComputeMaterial(R"(
        material {
            name: test_img_restrict,
            domain: compute,
            groupSize: [8, 8, 1],
            images: [ { name: img, format: r32f, qualifiers: [restrict] } ]
        }
        compute {
            void compute() {
                ivec2 c = ivec2(gl_GlobalInvocationID.xy);
                imageStore(img, c, imageLoad(img, c) + vec4(1.0));
            }
        }
    )");
}

TEST(TestParseAndComputeMaterial, ImageType2DArray) {
    expectValidComputeMaterial(R"(
        material {
            name: test_img_2d_array,
            domain: compute,
            groupSize: [4, 4, 1],
            images: [ { name: img, type: image2dArray, format: rgba8, qualifiers: [writeonly] } ]
        }
        compute {
            void compute() {
                ivec3 c = ivec3(gl_GlobalInvocationID.xyz);
                imageStore(img, c, vec4(1.0));
            }
        }
    )");
}

TEST(TestParseAndComputeMaterial, ImageType3D) {
    expectValidComputeMaterial(R"(
        material {
            name: test_img_3d,
            domain: compute,
            groupSize: [4, 4, 4],
            images: [ { name: img, type: image3d, format: r32f } ]
        }
        compute {
            void compute() {
                ivec3 c = ivec3(gl_GlobalInvocationID.xyz);
                imageStore(img, c, imageLoad(img, c) + vec4(1.0));
            }
        }
    )");
}

TEST(TestParseAndComputeMaterial, ImageTypeCubemap) {
    expectValidComputeMaterial(R"(
        material {
            name: test_img_cubemap,
            domain: compute,
            groupSize: [4, 4, 1],
            images: [ { name: img, type: imageCubemap, format: rgba8, qualifiers: [writeonly] } ]
        }
        compute {
            void compute() {
                ivec3 c = ivec3(gl_GlobalInvocationID.xyz);
                imageStore(img, c, vec4(1.0));
            }
        }
    )");
}

TEST(TestParseAndComputeMaterial, ImageTypeCubemapArray) {
    expectValidComputeMaterial(R"(
        material {
            name: test_img_cubemap_array,
            domain: compute,
            groupSize: [4, 4, 1],
            images: [ { name: img, type: imageCubemapArray, format: rgba8, qualifiers: [writeonly] } ]
        }
        compute {
            void compute() {
                ivec3 c = ivec3(gl_GlobalInvocationID.xyz);
                imageStore(img, c, vec4(1.0));
            }
        }
    )");
}


