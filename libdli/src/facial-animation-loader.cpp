/*
 * Copyright (c) 2020 Samsung Electronics Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

// FILE HEADER
#include "libdli/facial-animation-loader.h"

// INTERNAL INCLUDES
#include "libdli/blend-shape-details.h"
#include "libdli/json-reader.h"
#include "libdli/utils.h"

namespace js = json;

namespace dli
{

namespace
{
const float MILLISECONDS_TO_SECONDS = 0.001f;

struct BlendShape
{
  std::vector<std::vector<float>> mKeys;
  StringView mNodeName;
  uint32_t mNumberOfMorphTarget;
  StringView mVersion;
  StringView mFullName;
  std::vector<StringView> mMorphNames;
};

struct FacialAnimation
{
  StringView mName;
  std::vector<BlendShape> mBlendShapes;
  StringView mVersion;
  uint32_t mNumberOfShapes;
  std::vector<uint32_t> mTime;
  uint32_t mNumberOfFrames;
};

std::vector<std::vector<float>> ReadBlendShapeKeys(const json_value_s& j)
{
  auto& jo = js::Cast<json_array_s>(j);
  std::vector<std::vector<float>> result;

  result.reserve(jo.length);

  auto i = jo.start;
  while (i)
  {
    result.push_back(std::move(js::Read::Array<float, js::Read::Number>(*i->value)));
    i = i->next;
  }

  return result;
}

const auto BLEND_SHAPE_READER = std::move(js::Reader<BlendShape>()
  .Register(*js::MakeProperty("key", ReadBlendShapeKeys, &BlendShape::mKeys))
  .Register(*new js::Property<BlendShape, StringView>("name", js::Read::StringView, &BlendShape::mNodeName))
  .Register(*js::MakeProperty("morphtarget", js::Read::Number<uint32_t>, &BlendShape::mNumberOfMorphTarget))
  .Register(*new js::Property<BlendShape, StringView>("blendShapeVersion", js::Read::StringView, &BlendShape::mVersion))
  .Register(*new js::Property<BlendShape, StringView>("fullName", js::Read::StringView, &BlendShape::mFullName))
  .Register(*js::MakeProperty("morphname", js::Read::Array<StringView, js::Read::StringView>, &BlendShape::mMorphNames))
);

const auto FACIAL_ANIMATION_READER = std::move(js::Reader<FacialAnimation>()
  .Register(*new js::Property<FacialAnimation, StringView>("name", js::Read::StringView, &FacialAnimation::mName))
  .Register(*js::MakeProperty("blendShapes", js::Read::Array<BlendShape, js::ObjectReader<BlendShape>::Read>, &FacialAnimation::mBlendShapes))
  .Register(*new js::Property<FacialAnimation, StringView>("version", js::Read::StringView, &FacialAnimation::mVersion))
  .Register(*js::MakeProperty("shapesAmount", js::Read::Number<uint32_t>, &FacialAnimation::mNumberOfShapes))
  .Register(*js::MakeProperty("time", js::Read::Array<uint32_t, js::Read::Number>, &FacialAnimation::mTime))
  .Register(*js::MakeProperty("frames", js::Read::Number<uint32_t>, &FacialAnimation::mNumberOfFrames))
);

}// unnamed namespace

AnimationDefinition LoadFacialAnimation(const std::string& url)
{
  bool failed = false;
  auto js = LoadTextFile(url.c_str(), &failed);
  if (failed)
  {
    ExceptionFlinger(ASSERT_LOCATION) << "Failed to load " << url << ".";
  }

  json::unique_ptr root(json_parse(js.c_str(), js.size()));
  if (!root)
  {
    ExceptionFlinger(ASSERT_LOCATION) << "Failed to parse " << url << ".";
  }


  static bool setObjectReaders = true;
  if (setObjectReaders)
  {
    // NOTE: only referencing own, anonymous namespace, const objects; the pointers will never need to change.
    js::SetObjectReader(BLEND_SHAPE_READER);
    setObjectReaders = false;
  }

  auto& rootObj = js::Cast<json_object_s>(*root);

  FacialAnimation facialAnimation;
  FACIAL_ANIMATION_READER.Read(rootObj, facialAnimation);

  AnimationDefinition animationDefinition;
  animationDefinition.mName = facialAnimation.mName.ToString();
  animationDefinition.mDuration = MILLISECONDS_TO_SECONDS * static_cast<float>(facialAnimation.mTime[facialAnimation.mNumberOfFrames - 1u]);

  // Calculate the number of animated properties.
  uint32_t numberOfAnimatedProperties = 0u;
  for (const auto& blendShape : facialAnimation.mBlendShapes)
  {
    numberOfAnimatedProperties += blendShape.mNumberOfMorphTarget;
  }
  animationDefinition.mProperties.resize(numberOfAnimatedProperties);

  // Create the key frame instances.
  for (auto& animatedProperty : animationDefinition.mProperties)
  {
    animatedProperty.mKeyFrames = Dali::KeyFrames::New();
  }

  // Set the property names
  char weightNameBuffer[32];
  char* const pWeightName = weightNameBuffer + sprintf(weightNameBuffer, "%s", BlendShapes::WEIGHTS_UNIFORM.c_str());
  uint32_t targets = 0u;
  for (const auto& blendShape : facialAnimation.mBlendShapes)
  {
    for (uint32_t morphTargetIndex = 0u; morphTargetIndex < blendShape.mNumberOfMorphTarget; ++morphTargetIndex)
    {
      AnimatedProperty& animatedProperty = animationDefinition.mProperties[targets + morphTargetIndex];
      animatedProperty.mTimePeriod = Dali::TimePeriod(animationDefinition.mDuration);

      animatedProperty.mNodeName = blendShape.mNodeName;

      sprintf(pWeightName, "[%d]", morphTargetIndex);
      animatedProperty.mPropertyName = weightNameBuffer;
    }
    targets += blendShape.mNumberOfMorphTarget;
  }

  targets = 0u;
  for (const auto& blendShape : facialAnimation.mBlendShapes)
  {
    for (uint32_t timeIndex = 0u; timeIndex < facialAnimation.mNumberOfFrames; ++timeIndex)
    {
      const float progress = MILLISECONDS_TO_SECONDS * static_cast<float>(facialAnimation.mTime[timeIndex]) / animationDefinition.mDuration;

      for (uint32_t morphTargetIndex = 0u; morphTargetIndex < blendShape.mNumberOfMorphTarget; ++morphTargetIndex)
      {
        AnimatedProperty& animatedProperty = animationDefinition.mProperties[targets + morphTargetIndex];

        animatedProperty.mKeyFrames.Add(progress, blendShape.mKeys[timeIndex][morphTargetIndex]);
      }
    }
    targets += blendShape.mNumberOfMorphTarget;
  }

  return animationDefinition;
}

} // namespace dli