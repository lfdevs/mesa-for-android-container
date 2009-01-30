/*
 * Shader test program.
 * Load any vertex/fragment shader from file and try to draw something
 * with them.
 */



#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "GL/glut.h"
#include "readtex.h"
#include "extfuncs.h"
#include "shaderutil.h"

static const char *Prog = "ShaderTest";

static const char *VertFile = "multitex.vert";
static const char *FragFile = "multitex.frag";

static const char *TexFiles[2] = 
   {
      "../images/tile.rgb",
      "../images/tree2.rgba"
   };


static GLuint Program;

static GLfloat Xrot = 0.0, Yrot = .0, Zrot = 0.0;
static GLfloat EyeDist = 10;
static GLboolean Anim = GL_FALSE;


/* value[0] = tex unit */
static struct uniform_info Uniforms[] = {
   { "tex1",  1, GL_INT, { 0, 0, 0, 0 }, -1 },
   { "tex2",  1, GL_INT, { 1, 0, 0, 0 }, -1 },
   END_OF_UNIFORMS
};


static void
DrawPolygon(GLfloat size)
{
   glPushMatrix();

   glNormal3f(0, 0, 1);
   glBegin(GL_POLYGON);

   glNormal3f(-0.1, -0.1, 1);
   glMultiTexCoord2f(GL_TEXTURE0, 0, 0);
   glMultiTexCoord2f(GL_TEXTURE1, 0, 0);
   glVertex2f(-size, -size);

   glNormal3f(0.1, -0.1, 1);
   glMultiTexCoord2f(GL_TEXTURE0, 2, 0);
   glMultiTexCoord2f(GL_TEXTURE1, 1, 0);
   glVertex2f( size, -size);

   glNormal3f(0.1, 0.1, 1);
   glMultiTexCoord2f(GL_TEXTURE0, 2, 2);
   glMultiTexCoord2f(GL_TEXTURE1, 1, 1);
   glVertex2f( size,  size);

   glNormal3f(-0.1, 0.1, 1);
   glMultiTexCoord2f(GL_TEXTURE0, 0, 2);
   glMultiTexCoord2f(GL_TEXTURE1, 0, 1);
   glVertex2f(-size,  size);

   glEnd();
   glPopMatrix();
}


static void
draw(void)
{
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

   glPushMatrix(); /* modelview matrix */
      glTranslatef(0.0, 0.0, -EyeDist);
      glRotatef(Zrot, 0, 0, 1);
      glRotatef(Yrot, 0, 1, 0);
      glRotatef(Xrot, 1, 0, 0);

      DrawPolygon(3.0);

   glPopMatrix();

   glutSwapBuffers();
}


static void
idle(void)
{
   GLfloat t = 0.05 * glutGet(GLUT_ELAPSED_TIME);
   Yrot = t;
   glutPostRedisplay();
}


static void
key(unsigned char k, int x, int y)
{
   (void) x;
   (void) y;
   switch (k) {
   case ' ':
   case 'a':
      Anim = !Anim;
      if (Anim)
         glutIdleFunc(idle);
      else
         glutIdleFunc(NULL);
      break;
   case 'z':
      EyeDist -= 0.5;
      if (EyeDist < 3.0)
         EyeDist = 3.0;
      break;
   case 'Z':
      EyeDist += 0.5;
      if (EyeDist > 90.0)
         EyeDist = 90;
      break;
   case 27:
      exit(0);
   }
   glutPostRedisplay();
}


static void
specialkey(int key, int x, int y)
{
   GLfloat step = 2.0;
   (void) x;
   (void) y;
   switch (key) {
   case GLUT_KEY_UP:
      Xrot += step;
      break;
   case GLUT_KEY_DOWN:
      Xrot -= step;
      break;
   case GLUT_KEY_LEFT:
      Yrot -= step;
      break;
   case GLUT_KEY_RIGHT:
      Yrot += step;
      break;
   }
   glutPostRedisplay();
}


/* new window size or exposure */
static void
Reshape(int width, int height)
{
   GLfloat ar = (float) width / (float) height;
   glViewport(0, 0, (GLint)width, (GLint)height);
   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   glFrustum(-2.0*ar, 2.0*ar, -2.0, 2.0, 4.0, 100.0);
   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();
}


static void
InitTextures(void)
{
   GLenum filter = GL_LINEAR;
   int i;

   for (i = 0; i < 2; i++) {
      GLint imgWidth, imgHeight;
      GLenum imgFormat;
      GLubyte *image = NULL;

      image = LoadRGBImage(TexFiles[i], &imgWidth, &imgHeight, &imgFormat);
      if (!image) {
         printf("Couldn't read %s\n", TexFiles[i]);
         exit(0);
      }

      glActiveTexture(GL_TEXTURE0 + i);
      glBindTexture(GL_TEXTURE_2D, 42 + i);
      gluBuild2DMipmaps(GL_TEXTURE_2D, 4, imgWidth, imgHeight,
                        imgFormat, GL_UNSIGNED_BYTE, image);
      free(image);
      
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
   }
}


static GLuint
CreateProgram(const char *vertProgFile, const char *fragProgFile)
{
   GLuint fragShader, vertShader, program;

   vertShader = CompileShaderFile(GL_VERTEX_SHADER, vertProgFile);
   fragShader = CompileShaderFile(GL_FRAGMENT_SHADER, fragProgFile);
   assert(vertShader);
   program = LinkShaders(vertShader, fragShader);

   glUseProgram_func(program);

   return program;
}


static const char *
type_str(GLenum type)
{
   switch (type) {
   case GL_FLOAT:
      return "GL_FLOAT";
   case GL_FLOAT_VEC2:
      return "GL_FLOAT_VEC2";
   case GL_FLOAT_VEC3:
      return "GL_FLOAT_VEC3";
   case GL_FLOAT_VEC4:
      return "GL_FLOAT_VEC4";

   case GL_INT:
      return "GL_INT";
   case GL_INT_VEC2:
      return "GL_INT_VEC2";
   case GL_INT_VEC3:
      return "GL_INT_VEC3";
   case GL_INT_VEC4:
      return "GL_INT_VEC4";

   case GL_BOOL:
      return "GL_BOOL";
   case GL_BOOL_VEC2:
      return "GL_BOOL_VEC2";
   case GL_BOOL_VEC3:
      return "GL_BOOL_VEC3";
   case GL_BOOL_VEC4:
      return "GL_BOOL_VEC4";

   case GL_FLOAT_MAT2:
      return "GL_FLOAT_MAT2";
   case GL_FLOAT_MAT3:
      return "GL_FLOAT_MAT3";
   case GL_FLOAT_MAT4:
      return "GL_FLOAT_MAT4";

   case GL_SAMPLER_1D:
      return "GL_SAMPLER_1D";
   case GL_SAMPLER_2D:
      return "GL_SAMPLER_2D";
   case GL_SAMPLER_3D:
      return "GL_SAMPLER_3D";

   default:
      return "type?";
   }
}


static void
ExtractAttributes(GLuint program)
{
   GLint numAttribs, i;

   glGetProgramiv_func(program, GL_ACTIVE_ATTRIBUTES, &numAttribs);

   for (i = 0; i < numAttribs; i++) {
      char name[100];
      GLsizei len;
      GLint size;
      GLenum type;

      glGetActiveAttrib_func(program, i, sizeof(name),
                             &len, &size, &type, name);
      printf("Attrib %d: %s (size=%d, type=%s)\n",
             i, name, size, type_str(type));
   }
}

static void
ExtractUniforms(GLuint program)
{
   GLint numUniforms;
   GLuint i;

   glGetProgramiv_func(program, GL_ACTIVE_UNIFORMS, &numUniforms);

   for (i = 0; i < (GLuint) numUniforms; i++) {
      char name[100];
      GLsizei len;
      GLint size;
      GLenum type;

      glGetActiveUniform_func(program, i, sizeof(name),
                              &len, &size, &type, name);
      printf("Uniform %d: %s (size=%d, type=%s)\n",
             i, name, size, type_str(type));
   }
}



static void
InitPrograms(void)
{
   Program = CreateProgram(VertFile, FragFile);

   InitUniforms(Program, Uniforms);

   ExtractUniforms(Program);
   ExtractAttributes(Program);
}


static void
InitGL(void)
{
   const char *version = (const char *) glGetString(GL_VERSION);

   if (version[0] != '2' || version[1] != '.') {
      printf("Warning: this program expects OpenGL 2.0\n");
      /*exit(1);*/
   }
   printf("GL_RENDERER = %s\n",(const char *) glGetString(GL_RENDERER));

   GetExtensionFuncs();

   InitTextures();
   InitPrograms();

   glEnable(GL_DEPTH_TEST);

   glClearColor(.6, .6, .9, 0);
   glColor3f(1.0, 1.0, 1.0);
}


static void
Init(int argc, char *argv[])
{
   int i;
   for (i = 1; i < argc; i++) {
      if (strcmp(argv[i], "-fs") == 0) {
         FragFile = argv[i+1];
      }
      else if (strcmp(argv[i], "-vs") == 0) {
         VertFile = argv[i+1];
      }
   }
}


int
main(int argc, char *argv[])
{
   glutInit(&argc, argv);
   glutInitWindowSize(500, 400);
   glutInitDisplayMode(GLUT_RGB | GLUT_DEPTH | GLUT_DOUBLE);
   glutCreateWindow(Prog);
   glutReshapeFunc(Reshape);
   glutKeyboardFunc(key);
   glutSpecialFunc(specialkey);
   glutDisplayFunc(draw);
   if (Anim)
      glutIdleFunc(idle);
   Init(argc, argv);
   InitGL();
   glutMainLoop();
   return 0;
}
