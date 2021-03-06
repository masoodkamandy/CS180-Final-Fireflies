/*
 * CS180 Final Project Fireflies
 *
 * by MASOOD
 * 
 * Initiated: 5/31/2021
 * Last updated: 6/9/2021
 * 
 * */

#include <iostream>
#include <cmath>
#include <glad/glad.h>

#include "GLSL.h"
#include "Program.h"
#include "MatrixStack.h"
#include "Shape.h"
#include "WindowManager.h"
#include "GLTextureWriter.h"
#include "stb_image.h"
#include "Texture.h"
#include "Bezier.h"
#include "Spline.h"

#include "Common.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader/tiny_obj_loader.h>
#define PI 3.1415927

// value_ptr for glm
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp>

#define LOG(msg) \
	std::cout << msg << std::endl

#define LOG_MATRIX(mat) \
	std::cout << glm::to_string(mat) << std::endl;

using namespace std;
using namespace glm;
using namespace masood;

class Application : public EventCallbacks
{

public:
	WindowManager *windowManager = nullptr;

	// =======================================================================
	// DATA: GLOBAL SCENE PROPERTIES
	// =======================================================================

	int howManyTrees = 50;
	vector<glm::vec3> treePositions;
	vector<float> treeRotations;

	int howManySpheres = 250;
	float distanceThreshold = 0.000025f;

	// =======================================================================
	// DATA: SHADER PROGRAMS AND SHAPES
	// =======================================================================

	// Our shader program
	std::shared_ptr<Program> shaderGeometryPass;
	std::shared_ptr<Program> shaderLightingPass;
	std::shared_ptr<Program> shaderLightSphere;
	//std::shared_ptr<Program> shaderSkybox;

	// Shape to be used (from obj file)
	vector<shared_ptr<Shape>> tree;
	shared_ptr<Shape> sphere;
	shared_ptr<Shape> cube;

	// =======================================================================
	// DATA: TEXTURES (TO BE CONTINUED)
	// =======================================================================

	//the image to use as a texture (ground)
	shared_ptr<Texture> textureGround;
	shared_ptr<Texture> textureTreeMain;
	shared_ptr<Texture> textureTreeShell;

	// =======================================================================
	// DATA: GOODIES FOR DEFERRED SHADING
	// =======================================================================

	// Contains vertex information for OpenGL
	GLuint VertexArrayID;

	// Data necessary to give our triangle to OpenGL
	GLuint VertexBufferID;

	//geometry for texture render
	GLuint quad_VertexArrayID;
	GLuint quad_vertexbuffer;

	//reference to texture FBO
	GLuint gBuffer;
	GLuint gPosition, gNormal, gColorSpec;
	GLuint depthBuf;

	bool FirstTime = true;
	bool Defer = false;
	// int gMat = 0;

	// =======================================================================
	// DATA: GROUND PLANE
	// =======================================================================

	//global data for ground plane - direct load constant defined CPU data to GPU (not obj)
	GLuint GrndBuffObj, GrndNorBuffObj, GrndTexBuffObj, GIndxBuffObj;
	int g_GiboLen;
	//ground VAO
	GLuint GroundVertexArrayID;

	// =======================================================================
	// DATA: CAMERA CONTROL
	// =======================================================================

	//camera control - you can ignore - what matters is eye location and view matrix
	double g_phi, g_theta;
	vec3 view = vec3(0, 0, 1);
	vec3 strafe = vec3(1, 0, 0);
	vec3 g_eye = vec3(0, -0.5, 0);
	vec3 g_lookAt = vec3(0, -0.5, -1);
	bool MOVEF = false;
	bool MOVEB = false;
	bool MOVER = false;
	bool MOVEL = false;

	// =======================================================================
	// DATA: LIGHTING
	// =======================================================================

	vec3 g_light = vec3(2, 6, 6);
	vector<vec3> lightPositions;
	vector<vec3> lightColors;

	std::vector<Spline> fireflyPaths;

	// =======================================================================
	// DATA: SKYBOX
	// =======================================================================

	// vector<std::string> faces{
	// 	"px_right.tga",
	// 	"nx_left.tga",
	// 	"py_top.tga",
	// 	"ny_bottom.tga",
	// 	"pz_front.tga",
	// 	"nz_back.tga"};

	// unsigned int cubeMapTexture;

	// =======================================================================
	// RENDER LOOP
	// =======================================================================

	void render(float frametime)
	{
		// Get current frame buffer size.
		int width, height;
		glfwGetFramebufferSize(windowManager->getHandle(), &width, &height);
		glViewport(0, 0, width, height);

		//camera movement - made continuous while keypressed
		float speed = 0.01;
		if (MOVEL)
		{
			g_eye -= speed * strafe;
			g_lookAt -= speed * strafe;
		}
		else if (MOVER)
		{
			g_eye += speed * strafe;
			g_lookAt += speed * strafe;
		}
		else if (MOVEF)
		{
			g_eye -= speed * view;
			g_lookAt -= speed * view;
		}
		else if (MOVEB)
		{
			g_eye += speed * view;
			g_lookAt += speed * view;
		}

		// For deferred shading.
		glBindFramebuffer(GL_FRAMEBUFFER, gBuffer);

		// Clear framebuffer.
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		/* Leave this code to just draw the meshes alone */
		float aspect = width / (float)height;

		// Create the matrix stacks - please leave these alone for now
		auto Projection = make_shared<MatrixStack>();
		auto Model = make_shared<MatrixStack>();

		// Apply perspective projection. NEW
		Projection->pushMatrix();
		Projection->perspective(45.0f, aspect, 0.01f, 100.0f);

		/*
		*  GEOMETRY
		*/

		//Draw our scene - two meshes - right now to a texture
		shaderGeometryPass->bind();

		// Create the matrices
		glUniformMatrix4fv(shaderGeometryPass->getUniform("P"), 1, GL_FALSE, value_ptr(Projection->topMatrix()));
		SetView(shaderGeometryPass);

		// Trees
		// Grayish Brown: [Red:0.394 green:0.317 blue:0.250 alpha:1.0]
		SetMaterialColor(shaderGeometryPass, vec3(0.394, 0.317, 0.250));
		drawTrees(Model);

		// Ground
		textureGround->bind(shaderGeometryPass->getUniform("texture0"));
		drawGround(shaderGeometryPass);

		shaderGeometryPass->unbind();

		/* now draw the actual output */
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		/*
		*  LIGHTING
		*/

		shaderLightingPass->bind();

		glUniformMatrix4fv(shaderLightingPass->getUniform("P"), 1, GL_FALSE, value_ptr(Projection->topMatrix()));
		SetView(shaderLightingPass);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, gPosition);
		glUniform1i(shaderLightingPass->getUniform("texBuf"), 0); //
		// Masood addition
		glActiveTexture(GL_TEXTURE0 + 1);
		glBindTexture(GL_TEXTURE_2D, gNormal);
		glActiveTexture(GL_TEXTURE0 + 2);
		glBindTexture(GL_TEXTURE_2D, gColorSpec);
		glUniform1i(shaderLightingPass->getUniform("gPosition"), 0);
		glUniform1i(shaderLightingPass->getUniform("gNormal"), 1);
		glUniform1i(shaderLightingPass->getUniform("gColorSpec"), 2);
		glUniform3fv(
			shaderLightingPass->getUniform("lightPositions"),
			lightPositions.size(),
			reinterpret_cast<GLfloat *>(&lightPositions[0]));
		glUniform3fv(
			shaderLightingPass->getUniform("lightColors"),
			lightColors.size(),
			reinterpret_cast<GLfloat *>(&lightColors[0]));
		// LOG_MATRIX(g_eye);
		// When logging this to console, I realized that my position is being taken into account, but nowh
		glUniform3fv(shaderLightingPass->getUniform("viewPos"), 1, glm::value_ptr(g_eye));
		// End Masood addition
		glEnableVertexAttribArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, quad_vertexbuffer);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void *)0);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		glDisableVertexAttribArray(0);

		shaderLightingPass->unbind();

		/*code to write out the FBO (texture) just once -an example*/
		if (FirstTime)
		{
			assert(GLTextureWriter::WriteImage(gBuffer, "gBuf.png"));
			assert(GLTextureWriter::WriteImage(gPosition, "gPos.png"));
			assert(GLTextureWriter::WriteImage(gNormal, "gNorm.png"));
			assert(GLTextureWriter::WriteImage(gColorSpec, "gColorSpec.png"));
			assert(GLTextureWriter::WriteImage(depthBuf, "depthBuf.png"));
			FirstTime = false;
		}

		/*
		* COPY GEOMETRY'S DEPTH BUFFER TO DEFAULT FRAMEBUFFER'S DEPTH BUFFER
		*/

		glBindFramebuffer(GL_READ_FRAMEBUFFER, gBuffer);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0); // write to default framebuffer
		// blit to default framebuffer. Note that this may or may not work as the internal formats of both the FBO and default framebuffer have to match.
		// the internal formats are implementation defined. This works on all of my systems, but if it doesn't on yours you'll likely have to write to the
		// depth buffer in another shader stage (or somehow see to match the default framebuffer's internal format with the FBO's internal format).
		glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		/*
		* SPHERE LIGHTS
		*/

		shaderLightSphere->bind();
		glUniformMatrix4fv(shaderLightSphere->getUniform("P"), 1, GL_FALSE, value_ptr(Projection->topMatrix()));
		SetView(shaderLightSphere);

		drawSpheres(Model);
		updateFireflyPositionsUsingPaths(frametime);

		shaderLightSphere->unbind();

		/*
		*  SKYBOX
		*/

		// //to draw the sky box bind the right shader
		// shaderSkybox->bind();

		// //set the projection matrix - can use the same one
		// glUniformMatrix4fv(shaderSkybox->getUniform("P"), 1, GL_FALSE, value_ptr(Projection->topMatrix()));

		// //set the depth function to always draw the box!
		// glDepthFunc(GL_LEQUAL);

		// SetView(shaderSkybox);

		// //set and send model transforms - likely want a bigger cube
		// glUniformMatrix4fv(shaderSkybox->getUniform("M"), 1, GL_FALSE, value_ptr(Model->topMatrix()));

		// //bind the cube map texture
		// glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapTexture);

		// //draw the actual cube
		// //cube->draw(shaderSkybox);

		// drawSkycube(Model);

		// //set the depth test back to normal!
		// glDepthFunc(GL_LESS);

		// //unbind the shader for the skybox
		// shaderSkybox->unbind();

		// Pop matrix stacks.
		Projection->popMatrix();
	}

	// =======================================================================
	// SHADER INIT
	// =======================================================================

	void init(const std::string &resourceDirectory)
	{
		GLSL::checkVersion();

		g_phi = 0;
		g_theta = -3.14 / 2.0;

		// BACKGROUND COLOR
		glClearColor(0, 0, 0, 1.0f);
		// Enable z-buffer test.
		glEnable(GL_DEPTH_TEST);

		/*
		*  SKYBOX PROGRAM
		*/

		// shaderSkybox = make_shared<Program>();
		// shaderSkybox->setVerbose(false);
		// shaderSkybox->setShaderNames(
		// 	resourceDirectory + "/simple_vert.glsl",
		// 	resourceDirectory + "/tex_frag_skybox.glsl");
		// shaderSkybox->init();
		// shaderSkybox->addUniform("P");
		// shaderSkybox->addUniform("V");
		// shaderSkybox->addUniform("M");
		// shaderSkybox->addAttribute("vertPos");

		/*
		*  GEOMETRY PASS: THIS DRAWS ALL THE GEOMETRY AND SETS UP THE GBUFFERS
		*/

		shaderGeometryPass = make_shared<Program>();
		shaderGeometryPass->setVerbose(false);
		shaderGeometryPass->setShaderNames(
			resourceDirectory + "/simple_vert.glsl",
			resourceDirectory + "/gbuf_frag.glsl");
		if (!shaderGeometryPass->init())
		{
			std::cerr << "One or more shaders failed to compile... exiting!" << std::endl;
			exit(1);
		}
		shaderGeometryPass->addUniform("P");
		shaderGeometryPass->addUniform("V");
		shaderGeometryPass->addUniform("M");
		shaderGeometryPass->addUniform("texture0");
		shaderGeometryPass->addUniform("MatAmb");
		shaderGeometryPass->addUniform("MatDif");
		shaderGeometryPass->addAttribute("vertPos");
		shaderGeometryPass->addAttribute("vertNor");

		/*
		*  LIGHTING PASS: THIS IS WHERE DEFERRED SHADING HAPPENS
		*/

		shaderLightingPass = make_shared<Program>();
		shaderLightingPass->setVerbose(false);
		shaderLightingPass->setShaderNames(
			resourceDirectory + "/pass_vert.glsl",
			resourceDirectory + "/tex_frag_modified.glsl");
		if (!shaderLightingPass->init())
		{
			std::cerr << "One or more shaders failed to compile... exiting!" << std::endl;
			exit(1);
		}
		shaderLightingPass->addUniform("P");
		shaderLightingPass->addUniform("V");
		shaderLightingPass->addUniform("M");
		shaderLightingPass->addAttribute("vertPos");
		shaderLightingPass->addAttribute("vertTex");
		shaderLightingPass->addUniform("Ldir");
		shaderLightingPass->addUniform("lightPositions");
		shaderLightingPass->addUniform("lightColors");
		shaderLightingPass->addUniform("gPosition");
		shaderLightingPass->addUniform("gNormal");
		shaderLightingPass->addUniform("gColorSpec");

		/*
		*  SPHERE PASS: THE LIGHTS ARE PLACED ON TOP AFTER DEFERRED RENDERING
		*/

		shaderLightSphere = make_shared<Program>();
		shaderLightSphere->setVerbose(false);
		shaderLightSphere->setShaderNames(
			resourceDirectory + "/sphere_vert.glsl",
			resourceDirectory + "/sphere_frag.glsl");
		if (!shaderLightSphere->init())
		{
			std::cerr << "One or more shaders failed to compile... exiting!" << std::endl;
			exit(1);
		}
		shaderLightSphere->addUniform("P");
		shaderLightSphere->addUniform("V");
		shaderLightSphere->addUniform("M");
		shaderLightSphere->addUniform("texture0");
		shaderLightSphere->addUniform("MatAmb");
		shaderLightSphere->addUniform("MatDif");
		shaderLightSphere->addAttribute("vertPos");
		shaderLightSphere->addAttribute("vertNor");

		/*
		*  LOAD TEXTURES
		*/

		textureGround = make_shared<Texture>();
		textureGround->setFilename(resourceDirectory + "/ground.jpg");
		textureGround->init();
		textureGround->setUnit(0);
		textureGround->setWrapModes(GL_REPEAT, GL_REPEAT);

		textureTreeMain = make_shared<Texture>();
		textureTreeMain->setFilename(resourceDirectory + "/broadleaf_hero_field/Textures/Main_Bark_Color.jpg");
		textureTreeMain->init();
		textureTreeMain->setUnit(1);
		textureTreeMain->setWrapModes(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);

		textureTreeShell = make_shared<Texture>();
		textureTreeShell->setFilename(resourceDirectory + "/broadleaf_hero_field/Textures/Shell_Bark_Color.jpg");
		textureTreeShell->init();
		textureTreeShell->setUnit(2);
		textureTreeShell->setWrapModes(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);

		initBuffers();
	}

	// =======================================================================
	// BUFFER INIT
	// =======================================================================

	void initBuffers()
	{
		int width, height;
		glfwGetFramebufferSize(windowManager->getHandle(), &width, &height);

		//initialize the buffers -- from learnopengl.com
		glGenFramebuffers(1, &gBuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, gBuffer);

		// - position color buffer
		glGenTextures(1, &gPosition);
		glBindTexture(GL_TEXTURE_2D, gPosition);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gPosition, 0);

		// - normal color buffer
		glGenTextures(1, &gNormal);
		glBindTexture(GL_TEXTURE_2D, gNormal);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gNormal, 0);

		// - color + specular color buffer
		glGenTextures(1, &gColorSpec);
		glBindTexture(GL_TEXTURE_2D, gColorSpec);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gColorSpec, 0);

		glGenRenderbuffers(1, &depthBuf);
		//set up depth necessary as rendering a mesh that needs depth test
		glBindRenderbuffer(GL_RENDERBUFFER, depthBuf);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuf);

		// CREATE ANOTHER BUFFER JUST FOR THE SHAPE OF THE LIGHT

		//more FBO set up
		GLenum DrawBuffers[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
		glDrawBuffers(3, DrawBuffers);
	}

	// NOTE: This is never run. It must be for the framebuffer activity.

	void createFBO(GLuint &fb, GLuint &tex)
	{
		//initialize FBO
		int width, height;
		glfwGetFramebufferSize(windowManager->getHandle(), &width, &height);

		//set up framebuffer
		glBindFramebuffer(GL_FRAMEBUFFER, fb);
		//set up texture
		glBindTexture(GL_TEXTURE_2D, tex);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			cout << "Error setting up frame buffer - exiting" << endl;
			exit(0);
		}
	}

	/*
	* FOR RESIZING SCREEN
	*/

	void prepareFBO()
	{
		initBuffers();
	}

	// =======================================================================
	// GEOMETRY INIT
	// =======================================================================

	void initGeom(const std::string &resourceDirectory)
	{
		string errStr;
		// Initialize cube mesh.
		vector<tinyobj::shape_t> cubeShapes;
		vector<tinyobj::material_t> cubeMaterials;
		//load in the mesh and make the shape(s)
		bool rc = tinyobj::LoadObj(cubeShapes, cubeMaterials, errStr, (resourceDirectory + "/cube.obj").c_str());
		if (!rc)
		{
			cerr << errStr << endl;
		}
		else
		{
			normalizeGeometry(cubeShapes);
			cube = make_shared<Shape>();
			cube->createShape(cubeShapes[0]);
			cube->measure();
			cube->init();
		}

		/*
		* TREE MESH
		*/

		vector<tinyobj::shape_t> treeShapes;
		vector<tinyobj::material_t> treeMaterials;
		//load in the mesh and make the shape(s)
		rc = tinyobj::LoadObj(treeShapes, treeMaterials, errStr, (resourceDirectory + "/broadleaf_hero_field/OBJ format/broadleaf_hero_field.obj").c_str());
		if (!rc)
		{
			cerr << errStr << endl;
		}
		else
		{
			normalizeGeometry(treeShapes);
			for (unsigned int i = 0; i < treeShapes.size(); i++)
			{
				tree.push_back(make_shared<Shape>());
				tree[i]->createShape(treeShapes[i]);
				tree[i]->measure();
				tree[i]->init();
			}
		}

		// CREATE TREE POSITIONS
		for (int i = 0; i < howManyTrees; i++)
		{
			float x_lo = -5.0f;
			float x_hi = 5.0f;
			float x = x_lo + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (x_hi - x_lo)));

			float z_lo = -5.0f;
			float z_hi = 5.0f;
			float z = z_lo + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (z_hi - z_lo)));

			treePositions.push_back(glm::vec3(x, -1.25, z));

			float r_lo = 0.0f;
			float r_hi = 360.0f;
			float r = r_lo + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (r_hi - r_lo)));

			treeRotations.push_back(r);
		}

		/*
		* SPHERE MESH
		*/

		vector<tinyobj::shape_t> sphereShapes;
		vector<tinyobj::material_t> sphereMaterials;
		//load in the mesh and make the shape(s)
		rc = tinyobj::LoadObj(sphereShapes, sphereMaterials, errStr, (resourceDirectory + "/smoothSphere.obj").c_str());
		if (!rc)
		{
			cerr << errStr << endl;
		}
		else
		{
			normalizeGeometry(sphereShapes);
			sphere = make_shared<Shape>();
			sphere->createShape(sphereShapes[0]);
			sphere->measure();
			sphere->init();
		}

		// CREATE SPHERE POSITIONS
		for (int i = 0; i < howManySpheres; i++)
		{
			// Every time we create a sphere these are created.
			bool tooClose = false;

			float x, y, z;

			do
			{
				// Generate random floats.
				x = randFloat(-5.0f, 5.0f);
				y = randFloat(0.0f, 0.5f);
				z = randFloat(-5.0f, 5.0f);

				// Check that x,y,z are far enough away from already generated orb positions
				for (int i = 0; i < lightPositions.size(); i++)
				{
					if (glm::distance(lightPositions[i], glm::vec3(x, y, z)) < distanceThreshold)
					{
						tooClose = true;
						break;
					}
				}

			} while (tooClose == true);

			lightPositions.push_back(glm::vec3(x, y, z));
			lightColors.push_back(glm::vec3(randFloat(0.0, 1.0), randFloat(0.0, 1.0), randFloat(0.0, 1.0)));
		}

		// CREATE SPHERE SPLINE PATHS
		for (int i = 0; i < howManySpheres; i++)
		{
			// init splines up and down
			// Each firefly will have 4 positions. The first and last will be the same. The two middle ones will be random.
			// Create 4 offsets
			std::vector<float> randomOffset;

			float LO = -0.5f;
			float HI = 0.5f;

			for (int j = 0; j < 6; j++)
			{
				float r = LO + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (HI - LO)));
				randomOffset.push_back(r);
			}

			fireflyPaths.push_back(
				Spline(
					glm::vec3(lightPositions[i].x, lightPositions[i].y, lightPositions[i].z),
					glm::vec3(lightPositions[i].x + randomOffset[0], lightPositions[i].y + randomOffset[1], lightPositions[i].z + randomOffset[2]),
					glm::vec3(lightPositions[i].x + randomOffset[3], lightPositions[i].y + randomOffset[4], lightPositions[i].z + randomOffset[5]),
					glm::vec3(lightPositions[i].x, lightPositions[i].y, lightPositions[i].z),
					5,
					true));
		}

		// create skybox
		// cubeMapTexture = createSky(resourceDirectory + "/night/", faces);

		// Initialize the geometry to render a quad to the screen
		initQuad();
		initGround();
	}

	/**** geometry set up for a quad *****/
	void initQuad()
	{
		//now set up a simple quad for rendering FBO
		glGenVertexArrays(1, &quad_VertexArrayID);
		glBindVertexArray(quad_VertexArrayID);

		static const GLfloat g_quad_vertex_buffer_data[] =
			{
				-1.0f,
				-1.0f,
				0.0f,
				1.0f,
				-1.0f,
				0.0f,
				-1.0f,
				1.0f,
				0.0f,
				-1.0f,
				1.0f,
				0.0f,
				1.0f,
				-1.0f,
				0.0f,
				1.0f,
				1.0f,
				0.0f,
			};

		glGenBuffers(1, &quad_vertexbuffer);
		glBindBuffer(GL_ARRAY_BUFFER, quad_vertexbuffer);
		glBufferData(GL_ARRAY_BUFFER, sizeof(g_quad_vertex_buffer_data), g_quad_vertex_buffer_data, GL_STATIC_DRAW);
	}

	// Normalize Geometry
	// Source: https://github.com/tinyobjloader/tinyobjloader/issues/62
	double getGreatestDimension(std::vector<tinyobj::shape_t> &shapes)
	{
		// Find greatest (highest or lowest) value of position of vertex
		double greatest = 0.0;

		for (size_t i = 0; i < shapes.size(); i++)
		{
			assert((shapes[i].mesh.positions.size() % 3) == 0);
			for (size_t v = 0; v < shapes[i].mesh.positions.size() / 3; v++)
			{
				float x = abs(shapes[i].mesh.positions[3 * v + 0]);
				float y = abs(shapes[i].mesh.positions[3 * v + 1]);
				float z = abs(shapes[i].mesh.positions[3 * v + 2]);

				if (x > abs(greatest))
					greatest = x;
				if (y > abs(greatest))
					greatest = y;
				if (z > abs(greatest))
					greatest = z;
			}
		}

		return greatest;
	}

	// Normalize Geometry
	// Source: https://github.com/tinyobjloader/tinyobjloader/issues/62
	void normalizeGeometry(std::vector<tinyobj::shape_t> &shapes)
	{
		double greatest = getGreatestDimension(shapes);
		assert(greatest > 0.0);

		for (size_t i = 0; i < shapes.size(); i++)
		{
			for (size_t v = 0; v < shapes[i].mesh.positions.size() / 3; v++)
			{
				shapes[i].mesh.positions[3 * v + 0] /= greatest; // normalize x
				shapes[i].mesh.positions[3 * v + 1] /= greatest; // normalize y
				shapes[i].mesh.positions[3 * v + 2] /= greatest; // normalize z
			}
		}
	}

	// =======================================================================
	// SKYBOX
	// =======================================================================

	// unsigned int createSky(string dir, vector<string> faces)
	// {
	// 	unsigned int textureID;
	// 	glGenTextures(1, &textureID);
	// 	glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);
	// 	int width, height, nrChannels;
	// 	stbi_set_flip_vertically_on_load(false);
	// 	for (GLuint i = 0; i < faces.size(); i++)
	// 	{
	// 		unsigned char *data =
	// 			stbi_load((dir + faces[i]).c_str(), &width, &height, &nrChannels, 0);
	// 		if (data)
	// 		{
	// 			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
	// 						 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
	// 		}
	// 		else
	// 		{
	// 			cout << "failed to load: " << (dir + faces[i]).c_str() << endl;
	// 		}
	// 	}
	// 	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	// 	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	// 	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	// 	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	// 	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	// 	cout << " creating cube map any errors : " << glGetError() << endl;
	// 	return textureID;
	// }

	// void drawSkycube(shared_ptr<MatrixStack> Model)
	// {
	// 	Model->pushMatrix();
	// 	Model->loadIdentity();
	// 	Model->translate(vec3(0.0, -5.0, 0.0));
	// 	Model->scale(vec3(20.0, 20.0, 20.0));
	// 	setModel(shaderSkybox, Model);
	// 	cube->draw(shaderSkybox);
	// 	Model->popMatrix();
	// }

	// =======================================================================
	// GROUND PLANE
	// =======================================================================

	void initGround()
	{

		float g_groundSize = 100;
		float g_groundY = 0.0;

		// A x-z plane at y = g_groundY of dimension [-g_groundSize, g_groundSize]^2
		float GrndPos[] = {
			-g_groundSize, g_groundY, -g_groundSize,
			-g_groundSize, g_groundY, g_groundSize,
			g_groundSize, g_groundY, g_groundSize,
			g_groundSize, g_groundY, -g_groundSize};

		float GrndNorm[] = {
			0, 1, 0,
			0, 1, 0,
			0, 1, 0,
			0, 1, 0,
			0, 1, 0,
			0, 1, 0};

		static GLfloat GrndTex[] = {
			0, 0, // back
			0, 100,
			100, 100,
			100, 0};

		unsigned short idx[] = {0, 1, 2, 0, 2, 3};

		//generate the ground VAO
		glGenVertexArrays(1, &GroundVertexArrayID);
		glBindVertexArray(GroundVertexArrayID);

		g_GiboLen = 6;
		glGenBuffers(1, &GrndBuffObj);
		glBindBuffer(GL_ARRAY_BUFFER, GrndBuffObj);
		glBufferData(GL_ARRAY_BUFFER, sizeof(GrndPos), GrndPos, GL_STATIC_DRAW);

		glGenBuffers(1, &GrndNorBuffObj);
		glBindBuffer(GL_ARRAY_BUFFER, GrndNorBuffObj);
		glBufferData(GL_ARRAY_BUFFER, sizeof(GrndNorm), GrndNorm, GL_STATIC_DRAW);

		glGenBuffers(1, &GrndTexBuffObj);
		glBindBuffer(GL_ARRAY_BUFFER, GrndTexBuffObj);
		glBufferData(GL_ARRAY_BUFFER, sizeof(GrndTex), GrndTex, GL_STATIC_DRAW);

		glGenBuffers(1, &GIndxBuffObj);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, GIndxBuffObj);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
	}

	//code to draw the ground plane
	void drawGround(shared_ptr<Program> curS)
	{
		curS->bind();
		glBindVertexArray(GroundVertexArrayID);
		SetMaterial(curS, 1);
		//draw the ground plane
		SetModel(vec3(0, -1, 0), 0, 0, 1, curS);
		glEnableVertexAttribArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, GrndBuffObj);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);

		glEnableVertexAttribArray(1);
		glBindBuffer(GL_ARRAY_BUFFER, GrndNorBuffObj);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, 0);

		glEnableVertexAttribArray(2);
		glBindBuffer(GL_ARRAY_BUFFER, GrndTexBuffObj);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, 0);

		// draw!
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, GIndxBuffObj);
		glDrawElements(GL_TRIANGLES, g_GiboLen, GL_UNSIGNED_SHORT, 0);

		glDisableVertexAttribArray(0);
		glDisableVertexAttribArray(1);
		glDisableVertexAttribArray(2);
		curS->unbind();
	}

	// =======================================================================
	// MESH DRAW FUNCTIONS
	// =======================================================================

	/*
	* TREES
	*/

	void drawTrees(shared_ptr<MatrixStack> Model)
	{
		for (int i = 0; i < howManyTrees; i++)
		{
			Model->pushMatrix();
			Model->loadIdentity();
			Model->rotate(radians(treeRotations[i]), vec3(0, 1, 0));
			Model->translate(vec3(treePositions[i].x, treePositions[i].y + 0.25, treePositions[i].z));

			//draw the torso with these transforms
			Model->pushMatrix();
			Model->scale(vec3(5.0, 5.0, 5.0));
			setModel(shaderGeometryPass, Model);
			for (int i = 0; i < tree.size(); i++)
			{
				if (i == 2 || i == 1)
					continue;
				if (i == 0)
					textureTreeShell->bind(shaderGeometryPass->getUniform("Texture0"));
				if (i == 3)
					textureTreeMain->bind(shaderGeometryPass->getUniform("Texture0"));
				tree[i]->draw(shaderGeometryPass);
			}

			Model->popMatrix();

			Model->popMatrix();
		}
	}

	/*
	* SPHERES
	*/

	void drawSpheres(shared_ptr<MatrixStack> Model)
	{
		for (int i = 0; i < howManySpheres; i++)
		{
			Model->pushMatrix();
			Model->loadIdentity();
			Model->translate(vec3(lightPositions[i].x, lightPositions[i].y - 0.75f, lightPositions[i].z));

			//draw the torso with these transforms
			Model->pushMatrix();
			Model->scale(vec3(0.05, 0.05, 0.05));
			SetMaterialColor(shaderLightSphere, lightColors[i]);
			// glUniformMatrix4fv(prog->getUniform("M"), 1, GL_FALSE, value_ptr(Model->topMatrix()));
			setModel(shaderLightSphere, Model);
			sphere->draw(shaderLightSphere);
			Model->popMatrix();

			Model->popMatrix();
		}
	}

	// =======================================================================
	// ANIMATION FUNCTIONS
	// =======================================================================

	void updateFireflyPositionsUsingPaths(float frametime)
	{

		for (int i = 0; i < fireflyPaths.size(); i++)
		{
			fireflyPaths[i].update(frametime);
		}

		for (int i = 0; i < howManySpheres; i++)
		{
			lightPositions[i] = fireflyPaths[i].getPosition();
			if (fireflyPaths[i].isDone())
			{
				fireflyPaths[i].update(frametime);
			}
		}
	}

	// =======================================================================
	// HELPERS: MATERIALS & TRANSFORMATIONS
	// =======================================================================

	/* helper functions for sending matrix data to the GPU */
	mat4 SetProjectionMatrix(shared_ptr<Program> curShade)
	{
		int width, height;
		glfwGetFramebufferSize(windowManager->getHandle(), &width, &height);
		float aspect = width / (float)height;
		mat4 Projection = perspective(radians(50.0f), aspect, 0.1f, 100.0f);
		glUniformMatrix4fv(curShade->getUniform("P"), 1, GL_FALSE, value_ptr(Projection));
		return Projection;
	}

	// TODO: Figure out whether we can get rid of the dueling SetModel functions.

	void SetModel(shared_ptr<Program> curS, mat4 m)
	{
		glUniformMatrix4fv(curS->getUniform("M"), 1, GL_FALSE, value_ptr(m));
	}

	/* helper function to set model trasnforms */
	void SetModel(vec3 trans, float rotY, float rotX, float sc, shared_ptr<Program> curS)
	{
		mat4 Trans = glm::translate(glm::mat4(1.0f), trans);
		mat4 RotX = glm::rotate(glm::mat4(1.0f), rotX, vec3(1, 0, 0));
		mat4 RotY = glm::rotate(glm::mat4(1.0f), rotY, vec3(0, 1, 0));
		mat4 ScaleS = glm::scale(glm::mat4(1.0f), vec3(sc));
		mat4 ctm = Trans * RotX * RotY * ScaleS;
		glUniformMatrix4fv(curS->getUniform("M"), 1, GL_FALSE, value_ptr(ctm));
	}

	void setModel(std::shared_ptr<Program> prog, std::shared_ptr<MatrixStack> M)
	{
		glUniformMatrix4fv(prog->getUniform("M"), 1, GL_FALSE, value_ptr(M->topMatrix()));
	}

	/*normal game camera */
	void SetView(shared_ptr<Program> shader)
	{
		mat4 Cam = lookAt(g_eye, g_lookAt, vec3(0, 1, 0));

		glUniformMatrix4fv(shader->getUniform("V"), 1, GL_FALSE, value_ptr(Cam));
	}

	// /*normal game camera */
	// mat4 SetView(shared_ptr<Program> curShade)
	// {
	// 	mat4 Cam = lookAt(g_eye, g_lookAt, vec3(0, 1, 0));
	// 	glUniformMatrix4fv(curShade->getUniform("V"), 1, GL_FALSE, value_ptr(Cam));
	// 	return Cam;
	// }

	// helper function to set materials for shading
	void SetMaterial(shared_ptr<Program> curS, int i)
	{
		switch (i)
		{
		case 0: //shiny blue plastic
			glUniform3f(curS->getUniform("MatAmb"), 0.02f, 0.04f, 0.2f);
			glUniform3f(curS->getUniform("MatDif"), 0.0f, 0.16f, 0.9f);
			break;
		case 1: // flat grey
			glUniform3f(curS->getUniform("MatAmb"), 0.13f, 0.13f, 0.14f);
			glUniform3f(curS->getUniform("MatDif"), 0.3f, 0.3f, 0.4f);
			break;
		case 2: //brass
			glUniform3f(curS->getUniform("MatAmb"), 0.3294f, 0.2235f, 0.02745f);
			glUniform3f(curS->getUniform("MatDif"), 0.7804f, 0.5686f, 0.11373f);
			break;
		case 3: //copper
			glUniform3f(curS->getUniform("MatAmb"), 0.1913f, 0.0735f, 0.0225f);
			glUniform3f(curS->getUniform("MatDif"), 0.7038f, 0.27048f, 0.0828f);
			break;
		}
	}
	// helper function to set materials for shading
	void SetMaterialColor(shared_ptr<Program> curS, vec3 color)
	{
		glUniform3f(curS->getUniform("MatAmb"), color.r / 10.0f, color.g / 10.0f, color.b / 10.0f);
		glUniform3f(curS->getUniform("MatDif"), color.r, color.g, color.b);
	}

	// =======================================================================
	// INTERACTIVITY: KEYBOARD AND MOUSE
	// =======================================================================

	void mouseCallback(GLFWwindow *window, int button, int action, int mods)
	{
		cout << "use two finger mouse scroll" << endl;
	}

	/* much of the camera is here */
	void scrollCallback(GLFWwindow *window, double deltaX, double deltaY)
	{
		vec3 diff, newV;

		g_theta += deltaX * 0.01;
		g_phi += deltaY * 0.01;
		g_phi = constrain((double)g_phi, -M_PI / 2.0 + M_PI / 4.0, M_PI / 2.0 - M_PI / 4.0);

		newV.x = cosf(g_phi) * cosf(g_theta);
		newV.y = -1.0 * sinf(g_phi);
		newV.z = 1.0 * cosf(g_phi) * cosf((3.14 / 2.0 - g_theta));
		diff.x = (g_lookAt.x - g_eye.x) - newV.x;
		diff.y = (g_lookAt.y - g_eye.y) - newV.y;
		diff.z = (g_lookAt.z - g_eye.z) - newV.z;
		g_lookAt.x = g_lookAt.x - diff.x;
		g_lookAt.y = g_lookAt.y - diff.y;
		g_lookAt.z = g_lookAt.z - diff.z;
		view = g_eye - g_lookAt;
		strafe = cross(vec3(0, 1, 0), view);
	}

	void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
	{

		if (key == GLFW_KEY_A && action == GLFW_PRESS)
		{
			MOVEL = true;
		}
		if (key == GLFW_KEY_D && action == GLFW_PRESS)
		{
			MOVER = true;
		}
		if (key == GLFW_KEY_W && action == GLFW_PRESS)
		{
			MOVEF = true;
		}
		if (key == GLFW_KEY_S && action == GLFW_PRESS)
		{
			MOVEB = true;
		}
		if (key == GLFW_KEY_Q && action == GLFW_PRESS)
			g_light.x += 0.5;
		if (key == GLFW_KEY_E && action == GLFW_PRESS)
			g_light.x -= 0.5;
		if (key == GLFW_KEY_Z && action == GLFW_PRESS)
		{
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		}
		if (key == GLFW_KEY_Z && action == GLFW_RELEASE)
		{
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		}
		if (key == GLFW_KEY_P && action == GLFW_PRESS)
		{
			Defer = !Defer;
		}
		if (action == GLFW_RELEASE)
		{
			MOVER = MOVEF = MOVEB = MOVEL = false;
		}
	}

	void resizeCallback(GLFWwindow *window, int width, int height)
	{
		glViewport(0, 0, width, height);
		prepareFBO();
	}
};

// =======================================================================
// MAIN
// =======================================================================

int main(int argc, char **argv)
{
	// Where the resources are loaded from
	std::string resourceDir = "../resources";

	if (argc >= 2)
	{
		resourceDir = argv[1];
	}

	Application *application = new Application();

	// Your main will always include a similar set up to establish your window
	// and GL context, etc.

	WindowManager *windowManager = new WindowManager();
	windowManager->init(848, 480);
	windowManager->setEventCallbacks(application);
	application->windowManager = windowManager;

	glfwSetInputMode(windowManager->getHandle(), GLFW_CURSOR, GLFW_CURSOR_HIDDEN);

	// This is the code that will likely change program to program as you
	// may need to initialize or set up different data and state

	application->init(resourceDir);
	application->initGeom(resourceDir);

	auto lastTime = chrono::high_resolution_clock::now();

	// Loop until the user closes the window.
	while (!glfwWindowShouldClose(windowManager->getHandle()))
	{
		// save current time for next frame
		auto nextLastTime = chrono::high_resolution_clock::now();

		// get time since last frame
		float deltaTime =
			chrono::duration_cast<std::chrono::microseconds>(
				chrono::high_resolution_clock::now() - lastTime)
				.count();
		// convert microseconds (weird) to seconds (less weird)
		deltaTime *= 0.000001;

		// reset lastTime so that we can calculate the deltaTime
		// on the next frame
		lastTime = nextLastTime;

		// Render scene.
		application->render(deltaTime);

		// Swap front and back buffers.
		glfwSwapBuffers(windowManager->getHandle());
		// Poll for and process events.
		glfwPollEvents();
	}

	// Quit program.
	windowManager->shutdown();
	return 0;
}
