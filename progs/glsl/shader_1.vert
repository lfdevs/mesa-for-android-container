/* Shader 1 source */
// Multi-texture vertex shader
// Brian Paul


void main() 
{
   gl_TexCoord[0] = gl_MultiTexCoord0;
   gl_TexCoord[1] = gl_MultiTexCoord1;
   gl_Position = ftransform();
}

/* Compile status: ok */
/* GPU code */
# Vertex Program/Shader
  0: MOV OUTPUT[4], INPUT[8];
  1: MOV OUTPUT[5], INPUT[9];
  2: MUL TEMP[0], STATE[1], INPUT[0].yyyy;
  3: MAD TEMP[1], STATE[0], INPUT[0].xxxx, TEMP[0];
  4: MAD TEMP[0], STATE[2], INPUT[0].zzzz, TEMP[1];
  5: MAD OUTPUT[0], STATE[3], INPUT[0].wwww, TEMP[0];
  6: END
