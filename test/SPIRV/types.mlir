// RUN: mlir-opt -split-input-file -verify-diagnostics %s | FileCheck %s

// TODO(b/133530217): Add more tests after switching to the generic parser.

//===----------------------------------------------------------------------===//
// ArrayType
//===----------------------------------------------------------------------===//

// CHECK: func @scalar_array_type(!spv.array<16 x f32>, !spv.array<8 x i32>)
func @scalar_array_type(!spv.array<16xf32>, !spv.array<8 x i32>) -> ()

// CHECK: func @vector_array_type(!spv.array<32 x vector<4xf32>>)
func @vector_array_type(!spv.array< 32 x vector<4xf32> >) -> ()

// -----

// expected-error @+1 {{spv.array delimiter <...> mismatch}}
func @missing_left_angle_bracket(!spv.array 4xf32>) -> ()

// -----

// expected-error @+1 {{expected array element count followed by 'x' but found 'f32'}}
func @missing_count(!spv.array<f32>) -> ()

// -----

// expected-error @+1 {{expected array element count followed by 'x' but found 'f32'}}
func @missing_x(!spv.array<4 f32>) -> ()

// -----

// expected-error @+1 {{expected element type}}
func @missing_element_type(!spv.array<4x>) -> ()

// -----

// expected-error @+1 {{cannot parse type: blabla}}
func @cannot_parse_type(!spv.array<4xblabla>) -> ()

// -----

// expected-error @+1 {{cannot parse type: 3xf32}}
func @more_than_one_dim(!spv.array<4x3xf32>) -> ()

// -----

// expected-error @+1 {{only 1-D vector allowed but found 'vector<4x3xf32>'}}
func @non_1D_vector(!spv.array<4xvector<4x3xf32>>) -> ()

// -----

// expected-error @+1 {{cannot use 'tensor<4xf32>' to compose SPIR-V types}}
func @tensor_type(!spv.array<4xtensor<4xf32>>) -> ()

// -----

// expected-error @+1 {{cannot use 'bf16' to compose SPIR-V types}}
func @bf16_type(!spv.array<4xbf16>) -> ()

// -----

// expected-error @+1 {{only 8/16/32/64-bit integer type allowed but found 'i256'}}
func @i256_type(!spv.array<4xi256>) -> ()

// -----

// expected-error @+1 {{cannot use 'index' to compose SPIR-V types}}
func @index_type(!spv.array<4xindex>) -> ()

// -----

// expected-error @+1 {{cannot use '!llvm.i32' to compose SPIR-V types}}
func @llvm_type(!spv.array<4x!llvm.i32>) -> ()

// -----

//===----------------------------------------------------------------------===//
// PointerType
//===----------------------------------------------------------------------===//

// CHECK: func @scalar_ptr_type(!spv.ptr<f32, Uniform>)
func @scalar_ptr_type(!spv.ptr<f32, Uniform>) -> ()

// CHECK: func @vector_ptr_type(!spv.ptr<vector<4xi32>, PushConstant>)
func @vector_ptr_type(!spv.ptr<vector<4xi32>,PushConstant>) -> ()

// -----

// expected-error @+1 {{spv.ptr delimiter <...> mismatch}}
func @missing_left_angle_bracket(!spv.ptr f32, Uniform>) -> ()

// -----

// expected-error @+1 {{expected comma to separate pointee type and storage class in 'f32 Uniform'}}
func @missing_comma(!spv.ptr<f32 Uniform>) -> ()

// -----

// expected-error @+1 {{expected pointee type}}
func @missing_pointee_type(!spv.ptr<, Uniform>) -> ()

// -----

// expected-error @+1 {{unknown storage class: SomeStorageClass}}
func @unknown_storage_class(!spv.ptr<f32, SomeStorageClass>) -> ()

// -----

//===----------------------------------------------------------------------===//
// RuntimeArrayType
//===----------------------------------------------------------------------===//

// CHECK: func @scalar_runtime_array_type(!spv.rtarray<f32>, !spv.rtarray<i32>)
func @scalar_runtime_array_type(!spv.rtarray<f32>, !spv.rtarray<i32>) -> ()

// CHECK: func @vector_runtime_array_type(!spv.rtarray<vector<4xf32>>)
func @vector_runtime_array_type(!spv.rtarray< vector<4xf32> >) -> ()

// -----

// expected-error @+1 {{spv.rtarray delimiter <...> mismatch}}
func @missing_left_angle_bracket(!spv.rtarray f32>) -> ()

// -----

// expected-error @+1 {{expected element type}}
func @missing_element_type(!spv.rtarray<>) -> ()

// -----

// expected-error @+1 {{cannot parse type: 4xf32}}
func @redundant_count(!spv.rtarray<4xf32>) -> ()

// -----

//===----------------------------------------------------------------------===//
// ImageType
//===----------------------------------------------------------------------===//

// CHECK: func @image_parameters_1D(!spv.image<f32, 1D, NoDepth, NonArrayed, SingleSampled, SamplerUnknown, Unknown>)
func @image_parameters_1D(!spv.image<f32, 1D, NoDepth, NonArrayed, SingleSampled, SamplerUnknown, Unknown>) -> ()

// -----

// expected-error @+1 {{expected more parameters for image type 'f32'}}
func @image_parameters_one_element(!spv.image<f32>) -> ()

// -----

// expected-error @+1 {{expected more parameters for image type '1D'}}
func @image_parameters_two_elements(!spv.image<f32, 1D>) -> ()

// -----

// expected-error @+1 {{expected more parameters for image type 'NoDepth'}}
func @image_parameters_three_elements(!spv.image<f32, 1D, NoDepth>) -> ()

// -----

// expected-error @+1 {{expected more parameters for image type 'NonArrayed'}}
func @image_parameters_four_elements(!spv.image<f32, 1D, NoDepth, NonArrayed>) -> ()

// -----

// expected-error @+1 {{expected more parameters for image type 'SingleSampled'}}
func @image_parameters_five_elements(!spv.image<f32, 1D, NoDepth, NonArrayed, SingleSampled>) -> ()

// -----

// expected-error @+1 {{expected more parameters for image type 'SamplerUnknown'}}
func @image_parameters_six_elements(!spv.image<f32, 1D, NoDepth, NonArrayed, SingleSampled, SamplerUnknown>) -> ()

// -----

// expected-error @+1 {{spv.image delimiter <...> mismatch}}
func @image_parameters_delimiter(!spv.image f32, 1D, NoDepth, NonArrayed, SingleSampled, SamplerUnknown, Unkown>) -> ()

// -----

// expected-error @+1 {{unknown Dim in Image type: '1D NoDepth'}}
func @image_parameters_nocomma_1(!spv.image<f32, 1D NoDepth, NonArrayed, SingleSampled, SamplerUnknown, Unknown>) -> ()

// -----

// expected-error @+1 {{unknown ImageDepthInfo in Image type: 'NoDepth NonArrayed'}}
func @image_parameters_nocomma_2(!spv.image<f32, 1D, NoDepth NonArrayed, SingleSampled, SamplerUnknown, Unknown>) -> ()

// -----

// expected-error @+1 {{unknown ImageArrayedInfo in Image type: 'NonArrayed SingleSampled'}}
func @image_parameters_nocomma_3(!spv.image<f32, 1D, NoDepth, NonArrayed SingleSampled, SamplerUnknown, Unknown>) -> ()

// -----

// expected-error @+1 {{unknown ImageSamplingInfo in Image type: 'SingleSampled SamplerUnknown'}}
func @image_parameters_nocomma_4(!spv.image<f32, 1D, NoDepth, NonArrayed, SingleSampled SamplerUnknown, Unknown>) -> ()

// -----

// expected-error @+1 {{expected more parameters for image type 'SamplerUnknown Unknown'}}
func @image_parameters_nocomma_5(!spv.image<f32, 1D, NoDepth, NonArrayed, SingleSampled, SamplerUnknown Unknown>) -> ()

