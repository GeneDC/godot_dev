shader_type spatial;
render_mode unshaded, blend_mix;

uniform float start_fade_in = 1.5;
uniform 	float end_fade_in = 2.0;
uniform 	float start_fade_out = 64.0;
uniform 	float end_fade_out = 100.0;

varying vec3 v_world_normal;

void vertex() {
	// Pass the direction tag to the fragment shader
	v_world_normal = (MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz;
}

void fragment() {
	// 1. Cardinal Coloring
	// Use the absolute world normal to identify which axis the line is on
	vec3 abs_norm = abs(normalize(v_world_normal));
	vec3 color = vec3(0.5); // Default gray

	if (abs_norm.x > 0.9)      color = vec3(1.0, 0.0, 0.0); // X - Red
	else if (abs_norm.y > 0.9) color = vec3(0.0, 1.0, 0.0); // Y - Green
	else if (abs_norm.z > 0.9) color = vec3(0.0, 0.0, 1.0); // Z - Blue

	// Fade in so the it's not harsh
	float dist = -VERTEX.z; 
	float fadeIn = smoothstep(start_fade_in, end_fade_in, dist);
	float fadeOut = 1.0 - smoothstep(start_fade_out, end_fade_out, dist);
	float alpha = 1.0;//fadeIn * fadeOut; // fading currently not working right

	ALBEDO = color;
	ALPHA = alpha;
}