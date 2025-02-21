// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkSSAOPass.h"

#include "vtkInformation.h"
#include "vtkMatrix4x4.h"
#include "vtkObjectFactory.h"
#include "vtkOpenGLActor.h"
#include "vtkOpenGLCamera.h"
#include "vtkOpenGLError.h"
#include "vtkOpenGLFramebufferObject.h"
#include "vtkOpenGLPolyDataMapper.h"
#include "vtkOpenGLQuadHelper.h"
#include "vtkOpenGLRenderUtilities.h"
#include "vtkOpenGLRenderWindow.h"
#include "vtkOpenGLRenderer.h"
#include "vtkOpenGLShaderCache.h"
#include "vtkOpenGLState.h"
#include "vtkOpenGLVertexArrayObject.h"
#include "vtkRenderState.h"
#include "vtkRenderer.h"
#include "vtkShaderProgram.h"
#include "vtkVolume.h"
#include "vtkVolumeProperty.h"

#include <random>
#include <sstream>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSSAOPass);

//------------------------------------------------------------------------------
void vtkSSAOPass::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "FrameBufferObject:";
  if (this->FrameBufferObject != nullptr)
  {
    this->FrameBufferObject->PrintSelf(os, indent);
  }
  else
  {
    os << "(none)" << endl;
  }
  os << indent << "ColorTexture:";
  if (this->ColorTexture != nullptr)
  {
    this->ColorTexture->PrintSelf(os, indent);
  }
  else
  {
    os << "(none)" << endl;
  }
  os << indent << "PositionTexture:";
  if (this->PositionTexture != nullptr)
  {
    this->PositionTexture->PrintSelf(os, indent);
  }
  else
  {
    os << "(none)" << endl;
  }
  os << indent << "NormalTexture:";
  if (this->NormalTexture != nullptr)
  {
    this->NormalTexture->PrintSelf(os, indent);
  }
  else
  {
    os << "(none)" << endl;
  }
  os << indent << "SSAOTexture:";
  if (this->SSAOTexture != nullptr)
  {
    this->SSAOTexture->PrintSelf(os, indent);
  }
  else
  {
    os << "(none)" << endl;
  }
  os << indent << "DepthTexture:";
  if (this->DepthTexture != nullptr)
  {
    this->DepthTexture->PrintSelf(os, indent);
  }
  else
  {
    os << "(none)" << endl;
  }
}

//------------------------------------------------------------------------------
void vtkSSAOPass::InitializeGraphicsResources(vtkOpenGLRenderWindow* renWin, int w, int h)
{
  if (this->ColorTexture == nullptr)
  {
    this->ColorTexture = vtkTextureObject::New();
    this->ColorTexture->SetContext(renWin);
    this->ColorTexture->SetFormat(GL_RGBA);
    this->ColorTexture->SetInternalFormat(GL_RGBA32F);
    this->ColorTexture->SetDataType(GL_FLOAT);
    this->ColorTexture->SetMinificationFilter(vtkTextureObject::Linear);
    this->ColorTexture->SetMagnificationFilter(vtkTextureObject::Linear);
    this->ColorTexture->Allocate2D(w, h, 4, VTK_FLOAT);
  }

  if (this->PositionTexture == nullptr)
  {
    // This texture needs mipmapping levels in order to improve
    // texture sampling performances
    // see "Scalable ambient obscurance"
    this->PositionTexture = vtkTextureObject::New();
    this->PositionTexture->SetContext(renWin);
    this->PositionTexture->SetFormat(GL_RGBA);
    this->PositionTexture->SetInternalFormat(GL_RGBA16F);
    this->PositionTexture->SetDataType(GL_FLOAT);
    this->PositionTexture->SetWrapS(vtkTextureObject::ClampToEdge);
    this->PositionTexture->SetWrapT(vtkTextureObject::ClampToEdge);
    this->PositionTexture->SetMinificationFilter(vtkTextureObject::NearestMipmapNearest);
    this->PositionTexture->SetMaxLevel(10);
    this->PositionTexture->Allocate2D(w, h, 4, VTK_FLOAT);
  }

  if (this->NormalTexture == nullptr)
  {
    this->NormalTexture = vtkTextureObject::New();
    this->NormalTexture->SetContext(renWin);
    this->NormalTexture->SetFormat(GL_RGBA);
    this->NormalTexture->SetInternalFormat(GL_RGBA16F);
    this->NormalTexture->SetDataType(GL_FLOAT);
    this->NormalTexture->SetWrapS(vtkTextureObject::ClampToEdge);
    this->NormalTexture->SetWrapT(vtkTextureObject::ClampToEdge);
    this->NormalTexture->Allocate2D(w, h, 4, VTK_FLOAT);
  }

  if (this->SSAOTexture == nullptr)
  {
    this->SSAOTexture = vtkTextureObject::New();
    this->SSAOTexture->SetContext(renWin);
    this->SSAOTexture->SetFormat(GL_RED);
    this->SSAOTexture->SetInternalFormat(GL_R8);
    this->SSAOTexture->SetDataType(GL_UNSIGNED_BYTE);
    this->SSAOTexture->Allocate2D(w, h, 1, VTK_UNSIGNED_CHAR);
  }

  if (this->DepthTexture == nullptr)
  {
    this->DepthTexture = vtkTextureObject::New();
    this->DepthTexture->SetContext(renWin);
    this->DepthTexture->AllocateDepth(w, h, this->DepthFormat);
  }

  if (this->FrameBufferObject == nullptr)
  {
    this->FrameBufferObject = vtkOpenGLFramebufferObject::New();
    this->FrameBufferObject->SetContext(renWin);
  }
}

//------------------------------------------------------------------------------
void vtkSSAOPass::ComputeKernel()
{
  std::uniform_real_distribution<float> randomFloats(0.0, 1.0);
  std::default_random_engine generator;

  this->Kernel.resize(3 * this->KernelSize);

  for (unsigned int i = 0; i < this->KernelSize; ++i)
  {
    float sample[3] = { randomFloats(generator) * 2.f - 1.f, randomFloats(generator) * 2.f - 1.f,
      randomFloats(generator) };

    // reject the sample if not in the hemisphere
    if (vtkMath::Norm(sample) > 1.0)
    {
      i--;
      continue;
    }

    // more samples closer to the point
    float scale = i / static_cast<float>(this->KernelSize);
    scale = 0.1f + 0.9f * scale * scale; // lerp
    vtkMath::MultiplyScalar(sample, scale);

    this->Kernel[3 * i] = sample[0];
    this->Kernel[3 * i + 1] = sample[1];
    this->Kernel[3 * i + 2] = sample[2];
  }
}

//------------------------------------------------------------------------------
bool vtkSSAOPass::SetShaderParameters(vtkShaderProgram* vtkNotUsed(program),
  vtkAbstractMapper* mapper, vtkProp* vtkNotUsed(prop), vtkOpenGLVertexArrayObject* vtkNotUsed(VAO))
{
  if (vtkOpenGLPolyDataMapper::SafeDownCast(mapper) != nullptr ||
    mapper->IsA("vtkOpenGLGPUVolumeRayCastMapper"))
  {
    this->FrameBufferObject->ActivateDrawBuffers(3);
  }
  else
  {
    this->FrameBufferObject->ActivateDrawBuffers(1);
  }
  return true;
}

//------------------------------------------------------------------------------
void vtkSSAOPass::PreRenderProp(vtkProp* prop)
{
  // Create information and add the vtkOpenGLRenderPass information key
  this->Superclass::PreRenderProp(prop);

  vtkVolume* volume = vtkVolume::SafeDownCast(prop);
  if (volume)
  {
    // Shading must be enabled to compute normals
    if (!volume->GetProperty()->GetShade())
    {
      vtkErrorMacro("Shading must be enabled for volumes to support SSAO.");
    }

    vtkInformation* info = volume->GetPropertyKeys();
    info->Set(vtkOpenGLActor::GLDepthMaskOverride(), 1);
  }
}

//------------------------------------------------------------------------------
void vtkSSAOPass::PostRenderProp(vtkProp* prop)
{
  // Clean the vtkOpenGLRenderPass information key
  this->Superclass::PostRenderProp(prop);

  // Clean the GLDepthMaskOverride information key
  vtkVolume* volume = vtkVolume::SafeDownCast(prop);
  if (volume)
  {
    vtkInformation* info = volume->GetPropertyKeys();
    info->Remove(vtkOpenGLActor::GLDepthMaskOverride());
  }
}

//------------------------------------------------------------------------------
void vtkSSAOPass::RenderDelegate(const vtkRenderState* s, int w, int h)
{
  this->PreRender(s);

  this->FrameBufferObject->GetContext()->GetState()->PushFramebufferBindings();
  this->FrameBufferObject->Bind();

  this->FrameBufferObject->AddColorAttachment(0, this->ColorTexture);
  this->FrameBufferObject->AddColorAttachment(1, this->PositionTexture);
  this->FrameBufferObject->AddColorAttachment(2, this->NormalTexture);
  this->FrameBufferObject->ActivateDrawBuffers(3);
  this->FrameBufferObject->AddDepthAttachment(this->DepthTexture);
  this->FrameBufferObject->StartNonOrtho(w, h);

  vtkOpenGLRenderer* glRen = vtkOpenGLRenderer::SafeDownCast(s->GetRenderer());

  vtkOpenGLState* ostate = glRen->GetState();
  ostate->vtkglClear(GL_COLOR_BUFFER_BIT);
  ostate->vtkglDepthMask(GL_TRUE);
  ostate->vtkglClearDepth(1.0);
  ostate->vtkglClear(GL_DEPTH_BUFFER_BIT);

  this->DelegatePass->Render(s);
  this->NumberOfRenderedProps += this->DelegatePass->GetNumberOfRenderedProps();

  this->FrameBufferObject->RemoveColorAttachments(3);
  this->FrameBufferObject->RemoveDepthAttachment();

  this->FrameBufferObject->GetContext()->GetState()->PopFramebufferBindings();

  this->PostRender(s);
}

//------------------------------------------------------------------------------
void vtkSSAOPass::RenderSSAO(vtkOpenGLRenderWindow* renWin, vtkMatrix4x4* projection, int w, int h)
{
  if (this->SSAOQuadHelper && this->SSAOQuadHelper->ShaderChangeValue < this->GetMTime())
  {
    delete this->SSAOQuadHelper;
    this->SSAOQuadHelper = nullptr;
  }

  if (!this->SSAOQuadHelper)
  {
    this->ComputeKernel();

    std::string FSSource = vtkOpenGLRenderUtilities::GetFullScreenQuadFragmentShaderTemplate();

    std::stringstream ssDecl;
    ssDecl << "uniform sampler2D texPosition;\n"
              "uniform sampler2D texNormal;\n"
              "uniform sampler2D texNoise;\n"
              "uniform sampler2D texDepth;\n"
              "uniform float kernelRadius;\n"
              "uniform float kernelBias;\n"
              "uniform vec3 samples["
           << this->KernelSize
           << "];\n"
              "uniform mat4 matProjection;\n"
              "uniform ivec2 size;\n";

    vtkShaderProgram::Substitute(FSSource, "//VTK::FSQ::Decl", ssDecl.str());

    std::stringstream ssImpl;
    ssImpl
      << "\n"
         "  float occlusion = 0.0;\n"
         "  float depth = texture(texDepth, texCoord).r;\n"
         "  if (depth > 0.0 && depth < 1.0)\n" // discard background and overlay
         "  {\n"
         "    vec3 fragPosVC = texture(texPosition, texCoord).xyz;\n"
         "    vec4 fragPosDC = matProjection * vec4(fragPosVC, 1.0);\n"
         "    fragPosDC.xyz /= fragPosDC.w;\n"
         "    fragPosDC.xyz = fragPosDC.xyz * 0.5 + 0.5;\n"
         "    if (fragPosDC.z - depth < 0.0001)\n"
         "    {\n"
         "      vec3 normal = texture(texNormal, texCoord).rgb;\n"
         "      vec2 tilingShift = vec2(size) / vec2(textureSize(texNoise, 0));\n"
         "      float randomAngle = 6.283185 * texture(texNoise, texCoord * tilingShift).r;\n"
         "      vec3 randomVec = vec3(cos(randomAngle), sin(randomAngle), 0.0);\n"
         "      vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));\n"
         "      vec3 bitangent = cross(normal, tangent);\n"
         "      mat3 TBN = mat3(tangent, bitangent, normal);\n"
         "      const int kernelSize = "
      << this->KernelSize
      << ";\n"
         "      for (int i = 0; i < kernelSize; i++)\n"
         "      {\n"
         "        vec3 sampleVC = TBN * samples[i];\n"
         "        sampleVC = fragPosVC + sampleVC * kernelRadius;\n"
         "        vec4 sampleDC = matProjection * vec4(sampleVC, 1.0);\n"
         "        sampleDC.xyz /= sampleDC.w;\n"
         "        sampleDC.xyz = sampleDC.xyz * 0.5 + 0.5;\n" // to clip space
         "        float sampleDepth = textureLod(texPosition, sampleDC.xy, 40.0 * "
         "distance(fragPosDC.xy, sampleDC.xy)).z;\n"
         "        float rangeCheck = smoothstep(0.0, 1.0, kernelRadius / abs(fragPosVC.z - "
         "sampleDepth));\n"
         "        occlusion += (sampleDepth >= sampleVC.z + kernelBias ? 1.0 : 0.0) * rangeCheck;\n"
         "      }\n"
         "      occlusion = occlusion / float(kernelSize);\n"
         "    }\n"
         "  }\n"
         "  gl_FragData[0] = vec4(vec3(1.0 - occlusion), 1.0);\n";

    vtkShaderProgram::Substitute(FSSource, "//VTK::FSQ::Impl", ssImpl.str());

    this->SSAOQuadHelper = new vtkOpenGLQuadHelper(renWin,
      vtkOpenGLRenderUtilities::GetFullScreenQuadVertexShader().c_str(), FSSource.c_str(), "");

    this->SSAOQuadHelper->ShaderChangeValue = this->GetMTime();
  }
  else
  {
    renWin->GetShaderCache()->ReadyShaderProgram(this->SSAOQuadHelper->Program);
  }

  if (!this->SSAOQuadHelper->Program || !this->SSAOQuadHelper->Program->GetCompiled())
  {
    vtkErrorMacro("Couldn't build the SSAO shader program.");
    return;
  }

  this->PositionTexture->Activate();
  this->NormalTexture->Activate();
  this->DepthTexture->Activate();
  this->SSAOQuadHelper->Program->SetUniformi(
    "texPosition", this->PositionTexture->GetTextureUnit());
  this->SSAOQuadHelper->Program->SetUniformi("texNormal", this->NormalTexture->GetTextureUnit());
  this->SSAOQuadHelper->Program->SetUniform3fv("samples", this->KernelSize, this->Kernel.data());
  this->SSAOQuadHelper->Program->SetUniformi("texNoise", renWin->GetNoiseTextureUnit());
  this->SSAOQuadHelper->Program->SetUniformi("texDepth", this->DepthTexture->GetTextureUnit());
  this->SSAOQuadHelper->Program->SetUniformf("kernelRadius", this->Radius);
  this->SSAOQuadHelper->Program->SetUniformf("kernelBias", this->Bias);
  this->SSAOQuadHelper->Program->SetUniformMatrix("matProjection", projection);

  int size[2] = { w, h };
  this->SSAOQuadHelper->Program->SetUniform2i("size", size);

  this->FrameBufferObject->GetContext()->GetState()->PushFramebufferBindings();
  this->FrameBufferObject->Bind();

  this->FrameBufferObject->AddColorAttachment(0, this->SSAOTexture);
  this->FrameBufferObject->ActivateDrawBuffers(1);
  this->FrameBufferObject->StartNonOrtho(w, h);

  this->SSAOQuadHelper->Render();

  this->FrameBufferObject->RemoveColorAttachments(1);

  this->FrameBufferObject->GetContext()->GetState()->PopFramebufferBindings();

  this->DepthTexture->Deactivate();
  this->PositionTexture->Deactivate();
  this->NormalTexture->Deactivate();
}

//------------------------------------------------------------------------------
void vtkSSAOPass::RenderCombine(vtkOpenGLRenderWindow* renWin)
{
  vtkOpenGLState* ostate = renWin->GetState();

  if (this->CombineQuadHelper && this->CombineQuadHelper->ShaderChangeValue < this->GetMTime())
  {
    delete this->CombineQuadHelper;
    this->CombineQuadHelper = nullptr;
  }

  if (!this->CombineQuadHelper)
  {
    std::string FSSource = vtkOpenGLRenderUtilities::GetFullScreenQuadFragmentShaderTemplate();

    std::stringstream ssDecl;
    ssDecl << "uniform sampler2D texColor;\n"
              "uniform sampler2D texSSAO;\n"
              "uniform sampler2D texDepth;\n"
              "//VTK::FSQ::Decl";

    vtkShaderProgram::Substitute(FSSource, "//VTK::FSQ::Decl", ssDecl.str());

    std::stringstream ssImpl;
    ssImpl << "  vec4 col = texture(texColor, texCoord);\n";

    if (this->Blur)
    {
      ssImpl << "  ivec2 size = textureSize(texSSAO, 0);"
                "  float ao = 0.195346 * texture(texSSAO, texCoord).r + \n"
                "    0.077847 * texture(texSSAO, texCoord + vec2(-1, -1) / size).r +\n"
                "    0.077847 * texture(texSSAO, texCoord + vec2(-1, 1) / size).r +\n"
                "    0.077847 * texture(texSSAO, texCoord + vec2(1, -1) / size).r +\n"
                "    0.077847 * texture(texSSAO, texCoord + vec2(1, 1) / size).r +\n"
                "    0.123317 * texture(texSSAO, texCoord + vec2(-1, 0) / size).r +\n"
                "    0.123317 * texture(texSSAO, texCoord + vec2(1, 0) / size).r +\n"
                "    0.123317 * texture(texSSAO, texCoord + vec2(0, -1) / size).r +\n"
                "    0.123317 * texture(texSSAO, texCoord + vec2(0, 1) / size).r;\n";
    }
    else
    {
      ssImpl << "  float ao = texture(texSSAO, texCoord).r;\n";
    }
    ssImpl << "  gl_FragData[0] = vec4(col.rgb * ao, col.a);\n"
              "  gl_FragDepth = texture(texDepth, texCoord).r;\n";

    vtkShaderProgram::Substitute(FSSource, "//VTK::FSQ::Impl", ssImpl.str());

    this->CombineQuadHelper = new vtkOpenGLQuadHelper(renWin,
      vtkOpenGLRenderUtilities::GetFullScreenQuadVertexShader().c_str(), FSSource.c_str(), "");

    this->CombineQuadHelper->ShaderChangeValue = this->GetMTime();
  }
  else
  {
    renWin->GetShaderCache()->ReadyShaderProgram(this->CombineQuadHelper->Program);
  }

  if (!this->CombineQuadHelper->Program || !this->CombineQuadHelper->Program->GetCompiled())
  {
    vtkErrorMacro("Couldn't build the SSAO Combine shader program.");
    return;
  }

  this->ColorTexture->Activate();
  this->SSAOTexture->Activate();
  this->DepthTexture->Activate();
  this->CombineQuadHelper->Program->SetUniformi("texColor", this->ColorTexture->GetTextureUnit());
  this->CombineQuadHelper->Program->SetUniformi("texSSAO", this->SSAOTexture->GetTextureUnit());
  this->CombineQuadHelper->Program->SetUniformi("texDepth", this->DepthTexture->GetTextureUnit());

  ostate->vtkglEnable(GL_DEPTH_TEST);
  ostate->vtkglDepthFunc(GL_LEQUAL);
  ostate->vtkglClear(GL_DEPTH_BUFFER_BIT);

  this->CombineQuadHelper->Render();

  this->DepthTexture->Deactivate();
  this->ColorTexture->Deactivate();
  this->SSAOTexture->Deactivate();
}

//------------------------------------------------------------------------------
void vtkSSAOPass::Render(const vtkRenderState* s)
{
  vtkOpenGLClearErrorMacro();

  this->NumberOfRenderedProps = 0;

  vtkRenderer* r = s->GetRenderer();
  vtkOpenGLRenderWindow* renWin = static_cast<vtkOpenGLRenderWindow*>(r->GetRenderWindow());
  vtkOpenGLState* ostate = renWin->GetState();

  vtkOpenGLState::ScopedglEnableDisable bsaver(ostate, GL_BLEND);
  vtkOpenGLState::ScopedglEnableDisable dsaver(ostate, GL_DEPTH_TEST);

  if (this->DelegatePass == nullptr)
  {
    vtkWarningMacro("no delegate in vtkSSAOPass.");
    return;
  }

  // create FBO and texture
  int x = 0, y = 0, w, h;
  vtkFrameBufferObjectBase* fbo = s->GetFrameBuffer();
  if (fbo)
  {
    fbo->GetLastSize(w, h);
  }
  else
  {
    r->GetTiledSizeAndOrigin(&w, &h, &x, &y);
  }

  this->InitializeGraphicsResources(renWin, w, h);

  this->ColorTexture->Resize(w, h);
  this->PositionTexture->Resize(w, h);
  this->NormalTexture->Resize(w, h);
  this->SSAOTexture->Resize(w, h);
  this->DepthTexture->Resize(w, h);

  ostate->vtkglViewport(x, y, w, h);
  ostate->vtkglScissor(x, y, w, h);

  this->RenderDelegate(s, w, h);

  ostate->vtkglDisable(GL_BLEND);
  ostate->vtkglDisable(GL_DEPTH_TEST);

  // generate mipmap levels
  this->PositionTexture->Bind();
  glGenerateMipmap(GL_TEXTURE_2D);

  vtkOpenGLCamera* cam = (vtkOpenGLCamera*)(r->GetActiveCamera());
  vtkMatrix4x4* projection = cam->GetProjectionTransformMatrix(r->GetTiledAspectRatio(), -1, 1);
  projection->Transpose();

  this->RenderSSAO(renWin, projection, w, h);
  this->RenderCombine(renWin);

  vtkOpenGLCheckErrorMacro("failed after Render");
}

//------------------------------------------------------------------------------
bool vtkSSAOPass::PreReplaceShaderValues(std::string& vtkNotUsed(vertexShader),
  std::string& vtkNotUsed(geometryShader), std::string& fragmentShader, vtkAbstractMapper* mapper,
  vtkProp* vtkNotUsed(prop))
{
  // The mapper may be a vtkCompositePolyDataMapper, in that case, we should not return.
  // It is hard to determine if that CPDM uses OpenGL delegates. But if execution reaches
  // here, it is very likely that OpenGL classes are used.
  if (vtkPolyDataMapper::SafeDownCast(mapper) != nullptr)
  {
    // apply SSAO after lighting
    vtkShaderProgram::Substitute(fragmentShader, "//VTK::Light::Impl",
      "//VTK::Light::Impl\n"
      "  //VTK::SSAO::Impl\n",
      false);
  }

  if (mapper->IsA("vtkOpenGLGPUVolumeRayCastMapper"))
  {
    vtkShaderProgram::Substitute(fragmentShader, "//VTK::ComputeLighting::Dec",
      "vec3 g_dataNormal; \n"
      "//VTK::ComputeLighting::Dec\n",
      false);
    vtkShaderProgram::Substitute(fragmentShader, "//VTK::RenderToImage::Dec",
      "//VTK::RenderToImage::Dec\n"
      "  //VTK::SSAO::Dec\n",
      false);

    vtkShaderProgram::Substitute(fragmentShader, "//VTK::RenderToImage::Init",
      "//VTK::RenderToImage::Init\n"
      "  //VTK::SSAO::Init\n",
      false);

    vtkShaderProgram::Substitute(fragmentShader, "//VTK::RenderToImage::Impl",
      "//VTK::RenderToImage::Impl\n"
      "  //VTK::SSAO::Impl\n",
      false);

    vtkShaderProgram::Substitute(fragmentShader, "//VTK::RenderToImage::Exit",
      "//VTK::RenderToImage::Exit\n"
      "  //VTK::SSAO::Exit\n",
      false);
  }

  return true;
}

//------------------------------------------------------------------------------
bool vtkSSAOPass::PostReplaceShaderValues(std::string& vtkNotUsed(vertexShader),
  std::string& vtkNotUsed(geometryShader), std::string& fragmentShader, vtkAbstractMapper* mapper,
  vtkProp* vtkNotUsed(prop))
{
  // The mapper may be a vtkCompositePolyDataMapper, in that case, we should not return.
  // It is hard to determine if that CPDM uses OpenGL delegates. But if execution reaches
  // here, it is very likely that OpenGL classes are used.
  if (vtkPolyDataMapper::SafeDownCast(mapper) != nullptr)
  {
    if (fragmentShader.find("vertexVC") != std::string::npos &&
      fragmentShader.find("normalVCVSOutput") != std::string::npos)
    {
      vtkShaderProgram::Substitute(fragmentShader, "  //VTK::SSAO::Impl",
        "  gl_FragData[1] = vec4(vertexVC.xyz, 1.0);\n"
        "  gl_FragData[2] = vec4(normalVCVSOutput, 1.0);\n"
        "\n",
        false);
    }
    else
    {
      vtkShaderProgram::Substitute(fragmentShader, "  //VTK::SSAO::Impl",
        "  gl_FragData[1] = vec4(0.0, 0.0, 0.0, 0.0);\n"
        "  gl_FragData[2] = vec4(0.0, 0.0, 0.0, 0.0);\n"
        "\n",
        false);
    }
  }

  if (mapper->IsA("vtkOpenGLGPUVolumeRayCastMapper"))
  {
    vtkShaderProgram::Substitute(fragmentShader, "//VTK::SSAO::Dec",
      "vec3 l_ssaoFragNormal;\n"
      "vec3 l_ssaoFragPos;\n"
      "bool l_ssaoUpdateDepth;\n",
      false);

    vtkShaderProgram::Substitute(fragmentShader, "//VTK::SSAO::Init",
      "l_ssaoFragPos = vec3(-1.0);\n"
      "l_ssaoUpdateDepth = true;\n",
      false);

    std::stringstream ssaoImpl;
    ssaoImpl << "if (!g_skip && g_fragColor.a > " << this->VolumeOpacityThreshold
             << " && l_ssaoUpdateDepth)\n"
                "{\n"
                "  l_ssaoFragPos = g_dataPos;\n"
                "  l_ssaoFragNormal = g_dataNormal;\n"
                "  l_ssaoUpdateDepth = false;\n"
                "}";
    vtkShaderProgram::Substitute(fragmentShader, "//VTK::SSAO::Impl", ssaoImpl.str(), false);

    vtkShaderProgram::Substitute(fragmentShader, "//VTK::SSAO::Exit",
      "if (l_ssaoFragPos == vec3(-1.0))\n"
      "{\n"
      "  gl_FragDepth = 1.0;\n"
      "}\n"
      "else\n"
      "{\n"
      "  vec4 depthValue = in_projectionMatrix * in_modelViewMatrix *\n"
      "                    in_volumeMatrix[0] * in_textureDatasetMatrix[0] *\n"
      "                    vec4(l_ssaoFragPos, 1.0);\n"
      "  depthValue /= depthValue.w;\n"
      "  gl_FragDepth = 0.5 * (gl_DepthRange.far - gl_DepthRange.near) * depthValue.z + 0.5 * "
      "(gl_DepthRange.far + gl_DepthRange.near);\n"
      "  gl_FragData[1] = in_modelViewMatrix * in_volumeMatrix[0] * in_textureDatasetMatrix[0] * "
      "vec4(l_ssaoFragPos, 1.0);\n"
      "  gl_FragData[2] = vec4(normalize(l_ssaoFragNormal), 1.0);\n"
      "}",
      false);
  }

  vtkShaderProgram::Substitute(fragmentShader, "//VTK::ComputeLighting::Exit",
    "//VTK::ComputeLighting::Exit\n"
    "g_dataNormal = -shading_gradient.xyz;",
    false);

  return true;
}

//------------------------------------------------------------------------------
void vtkSSAOPass::ReleaseGraphicsResources(vtkWindow* w)
{
  this->Superclass::ReleaseGraphicsResources(w);

  if (this->SSAOQuadHelper)
  {
    delete this->SSAOQuadHelper;
    this->SSAOQuadHelper = nullptr;
  }
  if (this->CombineQuadHelper)
  {
    delete this->CombineQuadHelper;
    this->CombineQuadHelper = nullptr;
  }
  if (this->FrameBufferObject)
  {
    this->FrameBufferObject->Delete();
    this->FrameBufferObject = nullptr;
  }
  if (this->ColorTexture)
  {
    this->ColorTexture->Delete();
    this->ColorTexture = nullptr;
  }
  if (this->PositionTexture)
  {
    this->PositionTexture->Delete();
    this->PositionTexture = nullptr;
  }
  if (this->NormalTexture)
  {
    this->NormalTexture->Delete();
    this->NormalTexture = nullptr;
  }
  if (this->SSAOTexture)
  {
    this->SSAOTexture->Delete();
    this->SSAOTexture = nullptr;
  }
  if (this->DepthTexture)
  {
    this->DepthTexture->Delete();
    this->DepthTexture = nullptr;
  }
}
VTK_ABI_NAMESPACE_END
