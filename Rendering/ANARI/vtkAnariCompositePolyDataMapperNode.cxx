// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
#include "vtkAnariCompositePolyDataMapperNode.h"
#include "vtkAnariActorNode.h"
#include "vtkAnariProfiling.h"
#include "vtkAnariRendererNode.h"

#include "vtkActor.h"
#include "vtkCompositeDataDisplayAttributes.h"
#include "vtkCompositePolyDataMapper.h"
#include "vtkDataObject.h"
#include "vtkMultiBlockDataSet.h"
#include "vtkMultiPieceDataSet.h"
#include "vtkPolyData.h"
#include "vtkProperty.h"

VTK_ABI_NAMESPACE_BEGIN

//============================================================================
vtkStandardNewMacro(vtkAnariCompositePolyDataMapperNode);

//------------------------------------------------------------------------------
void vtkAnariCompositePolyDataMapperNode::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
void vtkAnariCompositePolyDataMapperNode::Invalidate(bool prepass)
{
  if (prepass)
  {
    this->RenderTime = 0;
  }
}

//------------------------------------------------------------------------------
void vtkAnariCompositePolyDataMapperNode::Render(bool prepass)
{
  vtkAnariProfiling startProfiling(
    "vtkAnariCompositePolyDataMapperNode::Render", vtkAnariProfiling::BROWN);

  if (prepass)
  {
    vtkAnariActorNode* anariActorNode = vtkAnariActorNode::SafeDownCast(this->Parent);
    vtkActor* act = vtkActor::SafeDownCast(anariActorNode->GetRenderable());

    if (act->GetVisibility() == false)
    {
      return;
    }

    auto anariRendererNode =
      static_cast<vtkAnariRendererNode*>(this->GetFirstAncestorOfType("vtkAnariRendererNode"));
    this->SetAnariConfig(anariRendererNode);
    vtkMTimeType inTime = anariActorNode->GetMTime();

    if (this->RenderTime >= inTime)
    {
      this->RenderSurfaceModels(false);
      return;
    }

    this->RenderTime = inTime;
    this->ClearSurfaces();
    vtkProperty* prop = act->GetProperty();

    // Push base-values on the state stack.
    this->BlockState.Visibility.push(true);
    this->BlockState.Opacity.push(prop->GetOpacity());
    this->BlockState.AmbientColor.push(vtkColor3d(prop->GetAmbientColor()));
    this->BlockState.DiffuseColor.push(vtkColor3d(prop->GetDiffuseColor()));
    this->BlockState.SpecularColor.push(vtkColor3d(prop->GetSpecularColor()));

    const char* materialName = prop->GetMaterialName();

    if (materialName != nullptr)
    {
      this->BlockState.Material.push(std::string(materialName));
    }
    else
    {
      this->BlockState.Material.push(std::string("matte"));
    }

    // render using the composite data attributes
    unsigned int flat_index = 0;
    vtkCompositePolyDataMapper* cpdm = vtkCompositePolyDataMapper::SafeDownCast(act->GetMapper());
    vtkDataObject* dobj = nullptr;

    if (cpdm)
    {
      dobj = cpdm->GetInputDataObject(0, 0);

      if (dobj)
      {
        this->RenderBlock(cpdm, act, dobj, flat_index);
        this->RenderSurfaceModels(true);
      }
    }

    this->BlockState.Visibility.pop();
    this->BlockState.Opacity.pop();
    this->BlockState.AmbientColor.pop();
    this->BlockState.DiffuseColor.pop();
    this->BlockState.SpecularColor.pop();
    this->BlockState.Material.pop();
  }
}

//------------------------------------------------------------------------------
void vtkAnariCompositePolyDataMapperNode::RenderBlock(
  vtkCompositePolyDataMapper* cpdm, vtkActor* actor, vtkDataObject* dobj, unsigned int& flat_index)
{
  vtkAnariProfiling startProfiling(
    "vtkAnariCompositePolyDataMapperNode::RenderBlock", vtkAnariProfiling::BROWN);

  vtkCompositeDataDisplayAttributes* cda = cpdm->GetCompositeDataDisplayAttributes();

  vtkProperty* prop = actor->GetProperty();
  vtkColor3d ecolor(prop->GetEdgeColor());

  bool overrides_visibility = (cda && cda->HasBlockVisibility(dobj));
  if (overrides_visibility)
  {
    this->BlockState.Visibility.push(cda->GetBlockVisibility(dobj));
  }

  bool overrides_opacity = (cda && cda->HasBlockOpacity(dobj));
  if (overrides_opacity)
  {
    this->BlockState.Opacity.push(cda->GetBlockOpacity(dobj));
  }

  bool overrides_color = (cda && cda->HasBlockColor(dobj));
  if (overrides_color)
  {
    vtkColor3d color = cda->GetBlockColor(dobj);
    this->BlockState.AmbientColor.push(color);
    this->BlockState.DiffuseColor.push(color);
    this->BlockState.SpecularColor.push(color);
  }

  bool overrides_material = (cda && cda->HasBlockMaterial(dobj));
  if (overrides_material)
  {
    std::string material = cda->GetBlockMaterial(dobj);
    this->BlockState.Material.push(material);
  }

  // Advance flat-index. After this point, flat_index no longer points to this
  // block.
  flat_index++;
  vtkMultiBlockDataSet* mbds = vtkMultiBlockDataSet::SafeDownCast(dobj);
  vtkMultiPieceDataSet* mpds = vtkMultiPieceDataSet::SafeDownCast(dobj);

  if (mbds || mpds)
  {
    unsigned int numChildren = mbds ? mbds->GetNumberOfBlocks() : mpds->GetNumberOfPieces();

    for (unsigned int cc = 0; cc < numChildren; cc++)
    {
      vtkDataObject* child = mbds ? mbds->GetBlock(cc) : mpds->GetPiece(cc);

      if (child == nullptr)
      {
        // speeds things up when dealing with nullptr blocks (which is common with
        // AMRs).
        flat_index++;
        continue;
      }

      this->RenderBlock(cpdm, actor, child, flat_index);
    }
  }
  else if (dobj && this->BlockState.Visibility.top() == true &&
    this->BlockState.Opacity.top() > 0.0)
  {
    // do we have a entry for this dataset?
    // make sure we have an entry for this dataset
    vtkPolyData* ds = vtkPolyData::SafeDownCast(dobj);

    if (ds)
    {
      auto anariRendererNode =
        static_cast<vtkAnariRendererNode*>(this->GetFirstAncestorOfType("vtkAnariRendererNode"));
      vtkAnariActorNode* aNode = vtkAnariActorNode::SafeDownCast(this->Parent);
      vtkColor3d& aColor = this->BlockState.AmbientColor.top();
      vtkColor3d& dColor = this->BlockState.DiffuseColor.top();
      std::string& material = this->BlockState.Material.top();
      cpdm->ClearColorArrays(); // prevents reuse of stale color arrays

      double color[3] = { aColor.GetRed() * dColor.GetRed(), aColor.GetGreen() * dColor.GetGreen(),
        aColor.GetBlue() * dColor.GetBlue() };
      this->AnariRenderPoly(aNode, ds, color, this->BlockState.Opacity.top(), material);
    }
  }

  if (overrides_color)
  {
    this->BlockState.AmbientColor.pop();
    this->BlockState.DiffuseColor.pop();
    this->BlockState.SpecularColor.pop();
  }
  if (overrides_opacity)
  {
    this->BlockState.Opacity.pop();
  }
  if (overrides_visibility)
  {
    this->BlockState.Visibility.pop();
  }
  if (overrides_material)
  {
    this->BlockState.Material.pop();
  }
}

VTK_ABI_NAMESPACE_END
