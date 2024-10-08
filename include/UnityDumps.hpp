#pragma once
#include <array>
#include <cstdint>

struct MachineState {
	long X[30];
	float S[31];
	double D[31];
};

struct UnityData {
	const char* search_name;
	const char* output_name;
	char data_type; // w (int/uint), x (int64/uint64), s (float), d (double)
	bool get;
};

std::array unityDataStruct {
	UnityData{"UnityEngine.Time::get_maximumDeltaTime", "UnityEngine.Time::maximumDeltaTime", 's', true},
	UnityData{"UnityEngine.Time::get_maximumParticleDeltaTime", "UnityEngine.Time::maximumParticleDeltaTime", 's', true},
	UnityData{"UnityEngine.Time::get_fixedDeltaTime", "UnityEngine.Time::fixedDeltaTime", 's', true},
	UnityData{"UnityEngine.Application::get_targetFrameRate", "UnityEngine.Application::targetFrameRate", 'w', true},    
	UnityData{"UnityEngine.QualitySettings::get_pixelLightCount", "UnityEngine.QualitySettings::pixelLightCount", 'w', true},
	UnityData{"UnityEngine.QualitySettings::get_shadows", "UnityEngine.QualitySettings::shadows", 'w', true},
	UnityData{"UnityEngine.QualitySettings::get_shadowProjection", "UnityEngine.QualitySettings::shadowProjection", 'w', true},
	UnityData{"UnityEngine.QualitySettings::get_shadowCascades", "UnityEngine.QualitySettings::shadowCascades", 'w', true},
	UnityData{"UnityEngine.QualitySettings::get_shadowDistance", "UnityEngine.QualitySettings::shadowDistance", 's', true},
	UnityData{"UnityEngine.QualitySettings::get_shadowResolution", "UnityEngine.QualitySettings::shadowResolution", 'w', true},
	UnityData{"UnityEngine.QualitySettings::get_shadowmaskMode", "UnityEngine.QualitySettings::shadowmaskMode", 'w', true},
	UnityData{"UnityEngine.QualitySettings::get_shadowNearPlaneOffset", "UnityEngine.QualitySettings::shadowNearPlaneOffset", 's', true},
	UnityData{"UnityEngine.QualitySettings::get_shadowCascade2Split", "UnityEngine.QualitySettings::shadowCascade2Split", 's', true},
	UnityData{"UnityEngine.QualitySettings::get_lodBias", "UnityEngine.QualitySettings::lodBias", 's', true},
	UnityData{"UnityEngine.QualitySettings::get_anisotropicFiltering", "UnityEngine.QualitySettings::anisotropicFiltering", 'w', true},
	UnityData{"UnityEngine.QualitySettings::get_maximumLODLevel", "UnityEngine.QualitySettings::maximumLODLevel", 'w', true},
	UnityData{"UnityEngine.QualitySettings::get_particleRaycastBudget", "UnityEngine.QualitySettings::particleRaycastBudget", 'w', true},
	UnityData{"UnityEngine.QualitySettings::get_softParticles", "UnityEngine.QualitySettings::softParticles", 'w', true},
	UnityData{"UnityEngine.QualitySettings::get_softVegetation", "UnityEngine.QualitySettings::softVegetation", 'w', true},
	UnityData{"UnityEngine.QualitySettings::get_vSyncCount", "UnityEngine.QualitySettings::vSyncCount", 'w', true},
	UnityData{"UnityEngine.QualitySettings::get_antiAliasing", "UnityEngine.QualitySettings::antiAliasing", 'w', true}, //It's useless if it's 0 by default
	UnityData{"UnityEngine.QualitySettings::get_realtimeReflectionProbes", "UnityEngine.QualitySettings::realtimeReflectionProbes", 'w', true},
	UnityData{"UnityEngine.QualitySettings::get_billboardsFaceCameraPosition", "UnityEngine.QualitySettings::billboardsFaceCameraPosition", 'w', true},
	UnityData{"UnityEngine.QualitySettings::get_resolutionScalingFixedDPIFactor", "UnityEngine.QualitySettings::resolutionScalingFixedDPIFactor", 's', true},
	UnityData{"UnityEngine.QualitySettings::get_blendWeights", "UnityEngine.QualitySettings::blendWeights", 'w', true},
	UnityData{"UnityEngine.QualitySettings::get_skinWeights", "UnityEngine.QualitySettings::skinWeights", 'w', true},
	UnityData{"UnityEngine.QualitySettings::get_streamingMipmapsActive", "UnityEngine.QualitySettings::streamingMipmapsActive", 'w', true},
	UnityData{"UnityEngine.QualitySettings::get_streamingMipmapsMaxLevelReduction", "UnityEngine.QualitySettings::streamingMipmapsMaxLevelReduction", 'w', true},    
	UnityData{"UnityEngine.RenderSettings::get_fog", "UnityEngine.RenderSettings::fog", 'w', true},    
	UnityData{"UnityEngine.RenderSettings::get_fogStartDistance", "UnityEngine.RenderSettings::fogStartDistance", 's', true},   
	UnityData{"UnityEngine.RenderSettings::get_fogEndDistance", "UnityEngine.RenderSettings::fogEndDistance", 's', true},
	UnityData{"UnityEngine.RenderSettings::get_fogMode", "UnityEngine.RenderSettings::fogMode", 'w', true},    
	UnityData{"UnityEngine.RenderSettings::get_fogDensity", "UnityEngine.RenderSettings::fogDensity", 's', true},    
	UnityData{"UnityEngine.RenderSettings::get_ambientMode", "UnityEngine.RenderSettings::ambientMode", 'w', true},    
	UnityData{"UnityEngine.RenderSettings::get_ambientIntensity", "UnityEngine.RenderSettings::ambientIntensity", 's', true},   
	UnityData{"UnityEngine.RenderSettings::get_reflectionIntensity", "UnityEngine.RenderSettings::reflectionIntensity", 's', true},    
 	UnityData{"UnityEngine.RenderSettings::get_reflectionBounces", "UnityEngine.RenderSettings::reflectionBounces", 'w', true},    
 	UnityData{"UnityEngine.RenderSettings::get_defaultReflectionMode", "UnityEngine.RenderSettings::defaultReflectionMode", 'w', true},    
 	UnityData{"UnityEngine.RenderSettings::get_defaultReflectionResolution", "UnityEngine.RenderSettings::defaultReflectionResolution", 'w', true},    
 	UnityData{"UnityEngine.RenderSettings::get_haloStrength", "UnityEngine.RenderSettings::haloStrength", 's', true},    
 	UnityData{"UnityEngine.RenderSettings::get_flareStrength", "UnityEngine.RenderSettings::flareStrength", 's', true}, 
 	UnityData{"UnityEngine.RenderSettings::get_flareFadeSpeed", "UnityEngine.RenderSettings::flareFadeSpeed", 's', true},  
 	UnityData{"UnityEngine.Texture::get_masterTextureLimit", "UnityEngine.Texture::masterTextureLimit", 'w', true},
 	UnityData{"UnityEngine.Texture::get_anisotropicFiltering", "UnityEngine.Texture::anisotropicFiltering", 'w', true}, 
 	UnityData{"UnityEngine.Rendering.GraphicsSettings::get_realtimeDirectRectangularAreaLights", "UnityEngine.Rendering.GraphicsSettings::realtimeDirectRectangularAreaLights", 'w', true},  
 	UnityData{"UnityEngine.Rendering.GraphicsSettings::get_lightsUseLinearIntensity", "UnityEngine.Rendering.GraphicsSettings::lightsUseLinearIntensity", 'w', true},  
 	UnityData{"UnityEngine.Rendering.GraphicsSettings::get_lightsUseColorTemperature", "UnityEngine.Rendering.GraphicsSettings::lightsUseColorTemperature", 'w', true},  
 	UnityData{"UnityEngine.Rendering.GraphicsSettings::get_cameraRelativeLightCulling", "UnityEngine.Rendering.GraphicsSettings::cameraRelativeShadowCulling", 'w', true},  
 	UnityData{"UnityEngine.LightmapSettings::get_lightmapsMode", "UnityEngine.LightmapSettings::lightmapsMode", 'w', true}
};