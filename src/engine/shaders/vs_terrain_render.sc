$input a_texcoord0
$output v_texcoord0

#include "terrain_common.sh"

BUFFER_RO(u_CulledSubdBuffer, uint, 2);
BUFFER_RO(u_VertexBuffer, vec4, 3);
BUFFER_RO(u_IndexBuffer, uint, 4);

void main()
{
	// get threadID (each key is associated to a thread)
	int threadID = gl_InstanceID;

	// get coarse triangle associated to the key
	uint primID = u_CulledSubdBuffer[threadID*2];

	vec4 v_in[3];

	v_in[0] = u_VertexBuffer[u_IndexBuffer[primID * 3    ]];
	v_in[1] = u_VertexBuffer[u_IndexBuffer[primID * 3 + 1]];
	v_in[2] = u_VertexBuffer[u_IndexBuffer[primID * 3 + 2]];

	// compute sub-triangle associated to the key
	uint key = u_CulledSubdBuffer[threadID*2+1];

	vec4 v[3];

	subd(key, v_in, v);

	// compute vertex location
	vec4 finalVertex = berp(v, a_texcoord0);

	finalVertex.x *= u_terrainHalfWidth;
	finalVertex.y *= u_terrainHalfHeight;

	// 计算高度位移
	finalVertex.z += dmap(finalVertex.xy);

	// 计算 UV 坐标（用于纹理采样）
	vec2 uv_frag;
	uv_frag.x = (finalVertex.x + u_terrainHalfWidth) / (2.0 * u_terrainHalfWidth);
	uv_frag.y = (finalVertex.y + u_terrainHalfHeight) / (2.0 * u_terrainHalfHeight);
	v_texcoord0 = uv_frag;

	gl_Position = mul(u_modelViewProj, finalVertex);
}