#
# texture splatting plugin
#

set(TARGET_GLSL
    Splat.Noise.glsl
    Splat.vert.model.glsl
    Splat.vert.view.glsl
    Splat.frag.glsl)

set(SHADERS_CPP "${CMAKE_CURRENT_BINARY_DIR}/AutoGenShaders.cpp")

configure_shaders(
    SplatShaders.cpp.in
    ${SHADERS_CPP}
    ${TARGET_GLSL} )

set(TARGET_SRC
	SplatPlugin.cpp
	SplatExtension.cpp
	SplatCatalog.cpp
	SplatTerrainEffect.cpp
	SplatCoverageLegend.cpp
	ModelSplatter.cpp
    ${SHADERS_CPP})
	
set(LIB_PUBLIC_HEADERS
	SplatCoverageLegend
	SplatCatalog
	SplatExtension
	SplatOptions
	SplatTerrainEffect
	ModelSplatter)
	
SET(TARGET_H
	${LIB_PUBLIC_HEADERS}
	SplatExport
	SplatShaders)

SET(TARGET_COMMON_LIBRARIES ${TARGET_COMMON_LIBRARIES}
    osgEarthUtil)
	
setup_extension(osgearth_splat)

# to install public driver includes:
set(LIB_NAME splat)

include(ModuleInstallOsgEarthExtensionIncludes OPTIONAL)

