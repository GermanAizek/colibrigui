@property( colibri_gui )

@piece( custom_vs_attributes )
	vulkan_layout( OGRE_NORMAL ) in float4 normal;

	@property( colibri_text )
		vulkan_layout( OGRE_TANGENT ) in uint tangent;
		vulkan_layout( OGRE_BLENDINDICES ) in uint2 blendIndices;
	@end
@end

@piece( custom_vs_preExecution )
	@property( !colibri_text )
		uint colibriDrawId = inVs_drawId + ((uint(inVs_vertexId) - worldMaterialIdx[inVs_drawId].w) / 54u);
		#undef finalDrawId
		#define finalDrawId colibriDrawId
	@end

	#define worldViewProj 1.0f

	gl_ClipDistance[0] = normal.x;
	gl_ClipDistance[1] = normal.y;
	gl_ClipDistance[2] = normal.z;
	gl_ClipDistance[3] = normal.w;

	@property( colibri_text )
		uint vertId = (uint(inVs_vertexId) - worldMaterialIdx[inVs_drawId].w) % 6u;
		outVs.uvText.x = (vertId <= 1u || vertId == 5u) ? 0.0f : float( blendIndices.x );
		outVs.uvText.y = (vertId == 0u || vertId >= 4u) ? 0.0f : float( blendIndices.y );
		outVs.pixelsPerRow		= blendIndices.x;
		outVs.glyphOffsetStart	= tangent;
	@end
@end

@end
