// Faceted gold marker (original eight faces with rounded edges): each flat facet
// reflects a bright sky or a warm ground by its orientation, and a horizon band
// sweeps across facets and edges as the game spins it.
#include "NativeMarker.hlsli"

float4 PSMain(float4 position : SV_Position, float3 normal : NORMAL) : SV_Target {
    const float3 n = normalize(normal);
    const float3 view = float3(0, 0, 1);
    const float3 key = normalize(float3(-.55f, .8f, 1.0f));
    const float3 fill = normalize(float3(.75f, .1f, .5f));
    const float3 r = reflect(-view, n);
    const float sky = smoothstep(-.35f, .85f, r.y);
    float3 env = lerp(float3(.55f, .38f, .08f), float3(1.0f, .96f, .80f), sky);
    env += float3(1.0f, .97f, .85f) * .5f * exp(-pow((r.y - .12f) / .07f, 2.0f));
    const float diffuse = max(dot(n, key), 0.0f);
    const float broad = pow(max(dot(n, normalize(key + view)), 0.0f), 16.0f);
    const float sharp = pow(max(dot(n, normalize(float3(-.35f, .65f, 1.5f) + view)), 0.0f), 70.0f);
    const float rim = pow(1.0f - abs(dot(n, view)), 3.0f);
    const float3 gold = float3(1.0f, .82f, .16f);
    float3 c = gold * (.14f + .36f * diffuse + .10f * max(dot(n, fill), 0.0f)) + gold * env * .62f;
    c += float3(1.0f, .92f, .65f) * broad * .32f + float3(1.0f, .98f, .9f) * sharp * .7f;
    c += float3(.35f, .24f, .06f) * rim;
    return float4(saturate(c), 1);
}
