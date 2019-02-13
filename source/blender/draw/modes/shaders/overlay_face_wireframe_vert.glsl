
uniform mat4 ProjectionMatrix;
uniform mat4 ModelViewMatrix;
uniform mat4 ModelMatrix;
uniform mat3 NormalMatrix;

uniform float wireStepParam;
uniform float ofs;

#ifndef USE_SCULPT
float get_edge_sharpness(float wd)
{
	return (wd == 1.0) ? 1.0 : ((wd == 0.0) ? -1.0 : (wd + wireStepParam));
}
#else
float get_edge_sharpness(float wd) { return 1.0; }
#endif

/* Geometry shader version */
#if defined(SELECT_EDGES) || defined(USE_GEOM)

in vec3 pos;
in vec3 nor;
in float wd; /* wiredata */

out float facing_g;
out float edgeSharpness_g;

void main()
{
	edgeSharpness_g = get_edge_sharpness(wd);

	mat4 projmat = ProjectionMatrix;
	projmat[3][2] -= ofs;

	gl_Position = projmat * (ModelViewMatrix * vec4(pos, 1.0));

	facing_g = normalize(NormalMatrix * nor).z;

#ifdef USE_WORLD_CLIP_PLANES
	world_clip_planes_calc_clip_distance((ModelMatrix * vec4(pos, 1.0)).xyz);
#endif
}

#else /* USE_GEOM */

in vec3 pos;
in vec3 nor;
in float wd;

out float facing;
flat out float edgeSharpness;

void main()
{
	mat4 projmat = ProjectionMatrix;
	projmat[3][2] -= ofs;

	gl_Position = projmat * (ModelViewMatrix * vec4(pos, 1.0));

	edgeSharpness = get_edge_sharpness(wd);

	facing = normalize(NormalMatrix * nor).z;

#ifdef USE_WORLD_CLIP_PLANES
	world_clip_planes_calc_clip_distance((ModelMatrix * vec4(pos, 1.0)).xyz);
#endif
}

#endif /* SELECT_EDGES */
