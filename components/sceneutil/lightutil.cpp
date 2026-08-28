#include "lightutil.hpp"

#include <osg/Light>
#include <osg/Group>
#include <osg/ComputeBoundsVisitor>

#include <components/esm/loadligh.hpp>
#include <components/fallback/fallback.hpp>

#include "lightmanager.hpp"
#include "lightcontroller.hpp"
#include "util.hpp"
#include "visitor.hpp"
#include "positionattitudetransform.hpp"

namespace SceneUtil
{

    void configureLight(osg::Light *light, float radius, bool isExterior)
    {
        float quadraticAttenuation = 0.f;
        float linearAttenuation = 0.f;
        float constantAttenuation = 0.f;

        static const bool useConstant = Fallback::Map::getBool("LightAttenuation_UseConstant");
        static const bool useLinear = Fallback::Map::getBool("LightAttenuation_UseLinear");
        static const bool useQuadratic = Fallback::Map::getBool("LightAttenuation_UseQuadratic");
        static const float constantValue = Fallback::Map::getFloat("LightAttenuation_ConstantValue");
        static const float linearValue = Fallback::Map::getFloat("LightAttenuation_LinearValue");
        static const float quadraticValue = Fallback::Map::getFloat("LightAttenuation_QuadraticValue");
        static const float linearRadiusMult = Fallback::Map::getFloat("LightAttenuation_LinearRadiusMult");
        static const float quadraticRadiusMult = Fallback::Map::getFloat("LightAttenuation_QuadraticRadiusMult");
        static const int linearMethod = Fallback::Map::getInt("LightAttenuation_LinearMethod");
        static const int quadraticMethod = Fallback::Map::getInt("LightAttenuation_QuadraticMethod");
        static const bool outQuadInLin = Fallback::Map::getBool("LightAttenuation_OutQuadInLin");

        if (useConstant)
            constantAttenuation = constantValue;

        if (useLinear)
        {
            linearAttenuation = linearMethod == 0 ? linearValue : 0.01f;
            float r = radius * linearRadiusMult;
            if (r && (linearMethod == 1 || linearMethod == 2))
                linearAttenuation = linearValue / std::pow(r, linearMethod);
        }

        if (useQuadratic && (!outQuadInLin || isExterior))
        {
            quadraticAttenuation = quadraticMethod == 0 ? quadraticValue : 0.01f;
            float r = radius * quadraticRadiusMult;
            if (r && (quadraticMethod == 1 || quadraticMethod == 2))
                quadraticAttenuation = quadraticValue / std::pow(r, quadraticMethod);
        }

        light->setConstantAttenuation(constantAttenuation);
        light->setLinearAttenuation(linearAttenuation);
        light->setQuadraticAttenuation(quadraticAttenuation);
    }

    osg::ref_ptr<LightSource> addLight(osg::Group* node, const ESM::Light* esmLight, unsigned int partsysMask, unsigned int lightMask, bool isExterior)
    {
        SceneUtil::FindByNameVisitor visitor("AttachLight");
        node->accept(visitor);

        osg::Group* attachTo = nullptr;
        if (visitor.mFoundNode)
        {
            attachTo = visitor.mFoundNode;
        }
        else
        {
            osg::ComputeBoundsVisitor computeBound;
            computeBound.setTraversalMask(~partsysMask);
            // We want the bounds of all children of the node, ignoring the node's local transformation
            // So do a traverse(), not accept()
            computeBound.traverse(*node);

            // PositionAttitudeTransform seems to be slightly faster than MatrixTransform
            osg::ref_ptr<SceneUtil::PositionAttitudeTransform> trans(new SceneUtil::PositionAttitudeTransform);
            trans->setPosition(computeBound.getBoundingBox().center());

            node->addChild(trans);

            attachTo = trans;
        }

        osg::ref_ptr<LightSource> lightSource = createLightSource(esmLight, lightMask, isExterior, osg::Vec4f(0,0,0,1));
        attachTo->addChild(lightSource);
        return lightSource;
    }

    osg::ref_ptr<LightSource> createLightSource(const ESM::Light* esmLight, unsigned int lightMask, bool isExterior, const osg::Vec4f& ambient)
    {
        osg::ref_ptr<SceneUtil::LightSource> lightSource (new SceneUtil::LightSource);
        osg::ref_ptr<osg::Light> light (new osg::Light);
        // Arena X004: native, fixed Light Improvements policy.
        //
        // Negative lights are a legacy Morrowind trick that subtracts colour.
        // That path is inconsistent with the modern shader/PBR pipeline (which
        // clamps negative radiance) and can produce black halos or abrupt seams.
        // Disable them at the scene-source level instead of mutating ESM data.
        const bool negative = (esmLight->mData.mFlags & ESM::Light::Negative) != 0;
        lightSource->setNodeMask(negative ? 0u : lightMask);

        // Keep the authored radius unchanged. Arena's shader light coverage is
        // already expanded independently, so multiplying the content radius here
        // would cause light leaking and increase the number of overlapping lights.
        const float radius = negative ? 0.f : esmLight->mData.mRadius;
        lightSource->setRadius(radius);

        configureLight(light, radius, isExterior);

        const osg::Vec4f diffuse = negative
            ? osg::Vec4f(0.f, 0.f, 0.f, 1.f)
            : SceneUtil::colourFromRGB(esmLight->mData.mColor);
        light->setDiffuse(diffuse);
        light->setAmbient(negative ? osg::Vec4f(0.f, 0.f, 0.f, 1.f) : ambient);
        light->setSpecular(osg::Vec4f(0,0,0,0));

        lightSource->setLight(light);

        // The original Light Improvements defaults disable all four brightness
        // animations. Keep the controller in LT_Normal so both buffered osg::Light
        // instances continue to receive the current colour every frame, but never
        // opt into Flicker/FlickerSlow/Pulse/PulseSlow from content flags.
        osg::ref_ptr<SceneUtil::LightController> ctrl (new SceneUtil::LightController);
        ctrl->setDiffuse(light->getDiffuse());
        lightSource->addUpdateCallback(ctrl);

        return lightSource;
    }
}
