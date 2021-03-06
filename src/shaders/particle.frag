R"(
#version 150

uniform float str_scale;
in vec4 base_color;
in vec2 txcoord;
in float strength;
out vec4 frag_color;

void main() {
  // use *some* core function to draw a fuzzy blob
  float rs = dot(txcoord, txcoord);

  // looks more like the compact Gaussian
  float s = 1.0f/(1.0f+16.0f*rs*rs*rs) - 0.06f;

  // looks more like a Gaussian
  //float s = 1.0f/(1.0f+16.0f*rs*rs) - 0.06f;

  // scale the color intensity by the particle strength
  frag_color = (str_scale * strength * s) * base_color;
}
)"
