$input v_texcoord0 // 这个是从顶点着色器传来的 UV 坐标，正好用于采样漫反射纹理

#include "terrain_common.sh" // 确保包含了 uniform 和 sampler 声明
uniform vec4 u_diffuseUvParams; // x: uv mode (0:none,1:swap,2:rotCW,3:rotCCW), yzw: reserved

void main()
{
    // 1. 计算法线 (来自坡度图)
    vec2 s = texture2D(u_SmapSampler, v_texcoord0).rg * u_DmapFactor;
    vec3 n = normalize(vec3(-s, 1.0)); // 得到表面法线

    // 2. 计算一个简单的光照因子 (例如，模拟来自上方的定向光 NdotL)
    //    这里直接用法线的 Z 分量作为简化的亮度因子，并加一点环境光
    float lightFactor = clamp(n.z, 0.1, 1.0); // 基础亮度，范围 0.1 到 1.0

    // 3. 采样漫反射纹理
    //    使用顶点着色器传入的 v_texcoord0 进行采样
    vec2 diffuseUv = v_texcoord0;
    if (u_diffuseUvParams.x > 2.5) {
        diffuseUv = vec2(1.0 - v_texcoord0.y, v_texcoord0.x);
    } else if (u_diffuseUvParams.x > 1.5) {
        diffuseUv = vec2(v_texcoord0.y, 1.0 - v_texcoord0.x);
    } else if (u_diffuseUvParams.x > 0.5) {
        diffuseUv = vec2(v_texcoord0.y, v_texcoord0.x);
    }
    vec4 diffuseTexColor = texture2D(u_DiffuseSampler, diffuseUv);

    // 4. 结合光照和纹理颜色
    //    将纹理颜色乘以光照因子
    vec3 finalColor = diffuseTexColor.rgb * lightFactor;

    // 5. 输出最终颜色
    //    可以使用纹理的 alpha 通道，如果需要的话 diffuseTexColor.a
    gl_FragColor = vec4(finalColor, 1.0);
}
