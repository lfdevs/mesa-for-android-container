/* Shader 2 source */
// Multi-texture fragment shader
// Brian Paul

// Composite second texture over first.
// We're assuming the 2nd texture has a meaningful alpha channel.

uniform sampler2D tex1;
uniform sampler2D tex2;

void main()
{
   vec4 t1 = texture2D(tex1, gl_TexCoord[0].xy);
   vec4 t2 = texture2D(tex2, gl_TexCoord[1].xy);
   gl_FragColor = mix(t1, t2, t2.w);
}

/* Compile status: ok */
/* GPU code */
# Fragment Program/Shader
  0: TEX TEMP[0], INPUT[4], texture[0], 2D;
  1: TEX TEMP[1], INPUT[5], texture[1], 2D;
  2: MOV TEMP[2].x, TEMP[1].wwww;
  3: LRP OUTPUT[0], TEMP[2].xxxx, TEMP[1], TEMP[0];
  4: END
