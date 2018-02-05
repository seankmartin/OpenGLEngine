#define GLM_ENABLE_EXPERIMENTAL
#include <Windows.h>
#include <mmsystem.h>

#include "glew/glew.h"
#include "freeglut/freeglut.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/transform.hpp"

#include "Camera.h"
#include "ModelMatrixTransformations.h"
#include "ModelLoader.h"
#include "ErrorHandling.h"
#include "Shader.h"
#include "Renderer.h"
#include "Object.h"
#include "Particle.h"

#include <iostream>
#include <math.h>
#include <fstream>
#include <string>
#include <string.h>

using namespace std;

Camera FPcamera(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
Camera TPcamera;

int window_height = 0;
int window_width = 0;
int mouse_x, mouse_y;

//A matrix that is updated in the keyboard function, allows moving model around in the scene
glm::mat4 model_transform = glm::mat4(1.0f);

//Holds the information loaded from the obj files
vector<core::SceneInfo> Scene;

//Modified in the mouse function
bool left_mouse_down;
bool middle_mouse_down;
bool use_quaternions = false;

//NOTE it might be more effective to refactor this to Enum and allow many camera types
bool use_fp_camera = false;

std::vector<physics::Plane* > cube;

void InitCube() {
  glm::vec3 right(5.0f, 0.0f, 0.0f);
  glm::vec3 top(0.0f, 5.0f, 0.0f);
  glm::vec3 front(0.0f, 0.0f, 5.0f);
  glm::vec3* planes[3] = { &right, &top, &front };
  for (auto &plane : planes) {
    physics::Plane* side = new physics::Plane;
    side->position_ = *plane;
    side->normal_ = -glm::normalize(*plane);
    side->threshold_ = 0.02;
    cube.push_back(side);
    side = new physics::Plane;
    side->position_ = -*plane;
    side->normal_ = glm::normalize(*plane);
    side->threshold_ = 0.02;
    cube.push_back(side);
  }
}

scene::Object* sky_box;
scene::Object* particle_mesh;

void InitSkyBox() {
    core::SceneInfo sceneinfo;
    char* filename = "res/Models/cube.obj";
    if (!sceneinfo.LoadModelFromFile(filename)) {
        fprintf(stderr, "ERROR: Could not load %s", filename);
        exit(-1);
    }
    sceneinfo.InitBuffersAndArrays();
    sky_box = sceneinfo.GetObject_(0);
    //TODO set the textures for the sky box
    const char* front = "res/Models/textures/Storforsen4/negz.jpg";
    const char* back = "res/Models/textures/Storforsen4/posz.jpg";
    const char* top = "res/Models/textures/Storforsen4/posy.jpg";
    const char* bottom = "res/Models/textures/Storforsen4/negy.jpg";
    const char* left = "res/Models/textures/Storforsen4/negx.jpg";
    const char* right = "res/Models/textures/Storforsen4/posx.jpg";
    scene::Texture* texture = new scene::Texture();
    texture->CreateCubeMap(front, back, top, bottom, left, right);
    sky_box->SetTexture(texture);
}

enum class eRenderType {
  RT_blinn,
  RT_cel,
  RT_minnaert,
  RT_reflection
};

eRenderType render_type = eRenderType::RT_blinn;

render::CommonShader* blinn_phong;
render::CommonShader* silhoutte;
render::CommonShader* cel;
render::CommonShader* minnaert;
render::Shader* cube_map;
render::CommonShader* reflection;

void CreateShaders() {
    const std::string shaderfile = "config/shadernames.txt";
    blinn_phong = new render::CommonShader("blinn_phong", shaderfile);
    silhoutte = new render::CommonShader("silhouette", shaderfile);
    cel = new render::CommonShader("cel", shaderfile);
    minnaert = new render::CommonShader("minnaert", shaderfile);
    reflection = new render::CommonShader("reflection", shaderfile);
    cube_map = new render::Shader("cube_map", shaderfile);
    cube_map->Bind();
    cube_map->SetUniform4fv("scale", glm::scale(glm::mat4(1.0f), glm::vec3(5.0f)));
}

void ReloadShaders() {
    blinn_phong->Reload();
    silhoutte->Reload();
    cel->Reload();
    minnaert->Reload();
    reflection->Reload();
    cube_map->Reload();
    cube_map->Bind();
    cube_map->SetUniform4fv("scale", glm::scale(glm::mat4(1.0f), glm::vec3(5.0f)));
}

//TODO change this so that a force knows how to apply itself
physics::Force gravity(glm::vec3(0.0f, -0.01f, 0.0f));
//TODO replace this particle with a pool
physics::ParticlePool particle_pool;
void LoadModels() {
  core::SceneInfo sceneinfo;
  char* filename = "res/Models/unit_sphere.obj";
  if (!sceneinfo.LoadModelFromFile(filename)) {
    fprintf(stderr, "ERROR: Could not load %s", filename);
    exit(-1);
  }
  sceneinfo.InitBuffersAndArrays();
  scene::Object* root = new scene::Object();
  root->SetTranslation(glm::vec3(0.0f, 0.0f, -20.0f));
  root->UpdateModelMatrix();
  float radius = 1.0f;
  particle_mesh = sceneinfo.GetObject_(0);
  particle_mesh->SetColour(glm::vec3(1.0f, 0.0f, 0.0f));
  particle_mesh->SetParent(root);
  particle_mesh->SetScale(glm::vec3(radius));
  //TODO move this to elsewhere in the code base
  physics::Particle* particle = particle_pool.Create(glm::vec3(0.0f), glm::vec3(0.2f, 0.3f, 0.0f), 1.0f,
    radius, 0.7f, 180, particle_mesh);
  gravity.AddParticle(particle);
}

void DrawSkyBox() {
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  cube_map->Bind();
  glm::mat4 view;
  if (use_fp_camera) {
    FPcamera.updatePosition(glm::vec3(model_transform * glm::vec4(-0.007894f, 2.238691f, 2.166406f, 1.0f)));
    FPcamera.updateDirection(glm::vec3(model_transform * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
    view = FPcamera.getRotation();
  }
  else {
    view = TPcamera.getMatrix();
  }
  cube_map->SetUniform4fv("view", view);
  glm::mat4 persp_proj = glm::perspective(glm::radians(45.0f), (float)window_width / (float)window_height, 0.1f, 100.0f);
  cube_map->SetUniform4fv("proj", persp_proj);
  render::Renderer::Draw(*sky_box);
  glDepthMask(GL_TRUE);
  glEnable(GL_CULL_FACE);
}

void UpdateScene() {
  // Wait until at least 16ms passed since start of last frame (Effectively caps framerate at ~60fps)
  static DWORD  last_time = 0;
  DWORD curr_time = timeGetTime();
  DWORD delta = (curr_time - last_time);
  if (delta > 16)
  {
    particle_pool.ClearForces();
    gravity.AccumulateForces();
    particle_pool.Update();
    particle_pool.HandleCollision(cube);
    delta = 0;
    last_time = curr_time;
    glutPostRedisplay();
  }
}

void RenderWithShader(render::Shader* shader) {
  shader->Bind();
  glm::mat4 view = TPcamera.getMatrix();
  shader->SetUniform4fv("view", view);
  glm::mat4 persp_proj = glm::perspective(glm::radians(45.0f), (float)window_width / (float)window_height, 0.1f, 100.0f);
  shader->SetUniform4fv("proj", persp_proj);
  //Draw the particles
  render::Renderer renderer(shader, view);
  particle_pool.Draw(renderer);
}

void Render() {
  render::Renderer::Clear();
  DrawSkyBox();
  switch (render_type) {
    case eRenderType::RT_blinn :
      RenderWithShader(blinn_phong);
      break;
    case eRenderType::RT_cel :
      glCullFace(GL_FRONT);
      silhoutte->Bind();
      silhoutte->SetUniform3f("const_colour", glm::vec3(0.0f));
      silhoutte->SetUniform1f("offset", 0.15f);
      RenderWithShader(silhoutte);
      cel->Bind();
      cel->SetUniform1f("num_shades", 8);
      cel->SetUniform3f("base_colour", glm::vec3(1.0f));
      glCullFace(GL_BACK);
      RenderWithShader(cel);
      break;
    case eRenderType::RT_minnaert :
      RenderWithShader(minnaert);
      break;
    case eRenderType::RT_reflection :
      RenderWithShader(reflection);
      break;
    default:
      break;
  }
  glutSwapBuffers();
}

void Keyboard(unsigned char key, int x, int y) {
  static ModelMatrixTransformations translationmatrices;
  static GLfloat xtranslate = -0.3f;
  bool changedmatrices = false;
  switch (key) {

  case 13: //Enter key
    use_fp_camera = !use_fp_camera;
    if(use_fp_camera) FPcamera.mouseUpdate(glm::vec2(mouse_x, mouse_y));
    else FPcamera.first_click = false;
    break;

  case 27: //ESC key
    exit(0);
    break;

  case 32: //Space key
    use_quaternions = !use_quaternions;
    changedmatrices = true;
    break;

    //Reload vertex and fragment shaders during runtime
  case 'P':
    ReloadShaders();
    std::cout << "Reloaded shaders" << endl;
    break;

    //Rotations
  case 'i':
    translationmatrices.UpdateRotate(glm::vec3(glm::radians(10.0f), 0.0f, 0.0f), use_quaternions);
    changedmatrices = true;
    break;
  case 'I':
    translationmatrices.UpdateRotate(glm::vec3(glm::radians(-10.0f), 0.0f, 0.0f), use_quaternions);
    changedmatrices = true;
    break;
  case 'j':
    translationmatrices.UpdateRotate(glm::vec3(0.0f, glm::radians(10.0f), 0.0f), use_quaternions);
    changedmatrices = true;
    break;
  case 'J':
    translationmatrices.UpdateRotate(glm::vec3(0.0f, glm::radians(-10.0f), 0.0f), use_quaternions);
    changedmatrices = true;
    break;
  case 'k':
    translationmatrices.UpdateRotate(glm::vec3(0.0f, 0.0f, glm::radians(10.0f)), use_quaternions);
    changedmatrices = true;
    break;
  case 'K':
    translationmatrices.UpdateRotate(glm::vec3(0.0f, 0.0f, glm::radians(-10.0f)), use_quaternions);
    changedmatrices = true;
    break;

    //Scalings
  case 'x':
    translationmatrices.UpdateScale(glm::vec3(-0.2f, 0.0f, 0.0f));
    changedmatrices = true;
    break;
  case 'X':
    translationmatrices.UpdateScale(glm::vec3(0.2f, 0.0f, 0.0f));
    changedmatrices = true;
    break;
  case 'y':
    translationmatrices.UpdateScale(glm::vec3(0.0f, -0.2f, 0.0f));
    changedmatrices = true;
    break;
  case 'Y':
    translationmatrices.UpdateScale(glm::vec3(0.0f, 0.2f, 0.0f));
    changedmatrices = true;
    break;
  case 'z':
    translationmatrices.UpdateScale(glm::vec3(0.0f, 0.0f, -0.2f));
    changedmatrices = true;
    break;
  case 'Z':
    translationmatrices.UpdateScale(glm::vec3(0.0f, 0.0f, 0.2f));
    changedmatrices = true;
    break;
  case 'r':
    translationmatrices.UpdateScale(glm::vec3(-0.2f, -0.2f, -0.2f));
    changedmatrices = true;
    break;
  case 'R':
    translationmatrices.UpdateScale(glm::vec3(0.2f, 0.2f, 0.2f));
    changedmatrices = true;
    break;

  //Camera Translations
  case 'd':
    TPcamera.Move(glm::vec3(1.0f, 0.0f, 0.0f));
    changedmatrices = true;
    break;
  case 'a':
    TPcamera.Move(glm::vec3(-1.0f, 0.0f, 0.0f));
    changedmatrices = true;
    break;
  case 'w':
    TPcamera.Move(glm::vec3(0.0f, 1.0f, 0.0f));
    changedmatrices = true;
    break;
  case 's':
    TPcamera.Move(glm::vec3(0.0f, -1.0f, 0.0f));
    changedmatrices = true;
    break;
  case 'q':
    TPcamera.Move(glm::vec3(0.0f, 0.0f, -1.0f));
    changedmatrices = true;
    break;
  case 'e':
    TPcamera.Move(glm::vec3(0.0f, 0.0f, 1.0f));
    changedmatrices = true;
    break;

  //model translations
  case 'D':
    translationmatrices.UpdateTranslate(glm::vec3(0.2f, 0.0f, 0.0f));
    changedmatrices = true;
    break;
  case 'A':
    translationmatrices.UpdateTranslate(glm::vec3(-0.2f, 0.0f, 0.0f));
    changedmatrices = true;
    break;
  case 'W':
    translationmatrices.UpdateTranslate(glm::vec3(0.0f, 0.2f, 0.0f));
    changedmatrices = true;
    break;
  case 'S':
    translationmatrices.UpdateTranslate(glm::vec3(0.0f, -0.2f, 0.0f));
    changedmatrices = true;
    break;
  case 'Q':
    translationmatrices.UpdateTranslate(glm::vec3(0.0f, 0.0f, -0.2f));
    changedmatrices = true;
    break;
  case 'E':
    translationmatrices.UpdateTranslate(glm::vec3(0.0f, 0.0f, 0.2f));
    changedmatrices = true;
    break;
  //change rendering mode
  case 'l':
    render_type = eRenderType::RT_blinn;
    break;
  case 'L':
    render_type = eRenderType::RT_reflection;
    break;
  case 't':
    render_type = eRenderType::RT_cel;
    break;
  case 'T':
    render_type = eRenderType::RT_minnaert;
    break;
  default:
    break;
  }
  //Apply the changes
  if (changedmatrices) {
    model_transform = translationmatrices.UpdateModelMatrix();
    glutPostRedisplay();
  }
}

void Mouse(int button, int state, int x, int y) {
  if (!use_fp_camera) {
    if (button == GLUT_LEFT && state == GLUT_DOWN)
    {
      left_mouse_down = true;
      TPcamera.mouseUpdate(glm::vec2(x, y));
    }
    if (button == GLUT_LEFT && state == GLUT_UP)
    {
      left_mouse_down = false;
      TPcamera.first_click = false;
    }
    if (button == GLUT_MIDDLE_BUTTON && state == GLUT_DOWN)
    {
      middle_mouse_down = true;
      TPcamera.mouseMove(glm::vec2(x, y));
    }
    if (button == GLUT_MIDDLE_BUTTON && state == GLUT_UP)
    {
      middle_mouse_down = false;
      TPcamera.first_click = false;
    }
  }
}

void MouseMovement(int x, int y) {
  if (!use_fp_camera) {
    if (left_mouse_down) {
      TPcamera.mouseUpdate(glm::vec2(x, y));
    }
    if (middle_mouse_down) {
      TPcamera.mouseMove(glm::vec2(x, y));
    }
  }
}

void PassiveMouseMovement(int x, int y) {
  if (use_fp_camera) {
    //NOTE - A potential solution could be to make FPcamera and TP camera children of camera class
    //And the update function is different in FPcamera
    //FPcamera.mouseUpdate(glm::vec2(x, y));
  }
  mouse_x = x;
  mouse_y = y;
}

void Init() {
  glClearColor(0.3f, 0.3f, 0.3f, 1.0f); //Grey clear colour
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  LoadModels();
  InitSkyBox();
  InitCube();
  CreateShaders();
}

void CleanUp() {
  delete(blinn_phong);
  delete(minnaert);
  delete(cel);
  delete(silhoutte);
  delete(cube_map);
  delete(reflection);
  for (int i = 0; i < Scene.size(); ++i)
  {
    for (int j = 0; j < Scene[i].GetNumMeshes(); ++j) {
      delete(Scene[i].GetObject_(j)->GetTexture());
      delete(Scene[i].GetObject_(j));
    }
  }
}

int main(int argc, char** argv) {
  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
  window_width = 1440;
  window_height = 810;
  glutInitWindowPosition(100, 100);//optional
  glutInitWindowSize(window_width, window_height); //optional
  glutCreateWindow("Plane Rotations - Sean Martin 13319354");

  glewExperimental = GL_TRUE;
  GLenum res = glewInit();
  // Check for any errors in initialising glew
  if (res != GLEW_OK) {
    fprintf(stderr, "Error: '%s'\n", glewGetErrorString(res));
    return 1;
  }
  //Make sure which version of openGL you can use!
  if (glewIsSupported("GL_VERSION_4_5")) std::cout << "GLEW version supports 4.5" << endl;
  else fprintf(stderr, "glew version is not 4.5\n");
  if (glewIsSupported("GL_VERSION_4_3")) std::cout << "GLEW version supports 4.3" << endl;
  else fprintf(stderr, "glew version is not 4.3\n");

  #ifdef GLM_FORCE_RADIANS
    fprintf(stderr, "Glm is forced using radians\n");
  #else
    fprintf(stderr, "Glm is not forced using radians\n");
  #endif

  glutDisplayFunc(Render);
  glutIdleFunc(UpdateScene);
  glutKeyboardFunc(Keyboard);
  glutMouseFunc(Mouse);
  glutMotionFunc(MouseMovement);
  glutPassiveMotionFunc(PassiveMouseMovement);
  Init();
  glutMainLoop();
  CleanUp();
  return 0;
}
