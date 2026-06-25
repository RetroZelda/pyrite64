/**
* @copyright 2026 - Max Bebök
* @license MIT
*
* Shared imgui helpers + material-settings UI used by both the Model editor and the
* Particle-Emitter editor. Header-only (functions are inline) so it has no .cpp.
*/
#pragma once

#include "libdragon.h"
#include "ccMapping.h"
#include "textureEditor.h"
#include "../../../imgui/helper.h"
#include "../../../../project/assets/material.h"

namespace Editor::Helpers
{
  inline constexpr auto Z_MODES = "None\0Read\0Write\0Read+Write\0";
  inline constexpr auto AA_MODES = "None\0Standard\0Reduced\0";

  inline constexpr auto DITHER_MODES = "Square / Square\0"
    "Square / Inv. Square\0"
    "Square / Noise\0"
    "Square / None\0"
    "Bayer / Bayer\0"
    "Bayer / Inv. Bayer\0"
    "Bayer / Noise\0"
    "Bayer / None\0"
    "Noise / Square\0"
    "Noise / Inv. Square\0"
    "Noise / Noise\0"
    "Noise / None\0"
    "None / Bayer\0"
    "None / Inv. Bayer\0"
    "None / Noise\0"
    "None / None\0";

  inline constexpr auto VERTEX_EFFECTS =
    "None\0"
    "Spherical UV\0"
    "Cel-shade Color\0"
    "Cel-shade Alpha\0"
    "Outline\0"
    "UV Offset\0";

  inline void toggleProp(const char* name, bool &propState, auto cb)
  {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();

    ImGui::PushFont(nullptr, 18.0_px);

    if(ImGui::IconButton(
      propState
      ? ICON_MDI_CHECKBOX_MARKED_CIRCLE
      : ICON_MDI_CHECKBOX_BLANK_CIRCLE_OUTLINE,
      {24_px,24_px},
      ImVec4{1,1,1,1}
    )) {
      propState = !propState;
    }
    ImGui::PopFont();
    ImGui::SameLine();

    ImGui::SameLine();
    ImGui::Text("%s", name);
    ImGui::TableSetColumnIndex(1);

    if(!propState)ImGui::BeginDisabled();
    ImGui::PushID(name);
    cb();
    ImGui::PopID();
    if(!propState)ImGui::EndDisabled();
  }

  template<typename T>
  inline void toggleProp(const char* name, bool &propState, Property<T> &prop)
  {
    toggleProp(name, propState, [&prop](){
      ImTable::typedInput(&prop.value);
    });
  }

  inline void printCC(const char* a, const char* b, const char* c, const char* d)
  {
    auto nonZero = [](const char* s){ return s[0] != '0'; };

    std::string s{};
    // check if mul does something
    if(nonZero(c) && (nonZero(a) || nonZero(b)))
    {
      if(nonZero(a) && nonZero(b)) {
        s += std::string{"("} + a + " - " + b + ")";
      } else {
        s += nonZero(a) ? a : b;
      }
      s += std::string{" * "} + c;
    }

    if(nonZero(d)) {
      if(!s.empty())s += " + ";
      s += d;
    }
    if(s.empty())s = "0";

    ImGui::Text("%s", s.c_str());
  }

  /**
   * Draws all material render/blend settings (color-combiner, sampling, values,
   * geometry modes, render modes) into the current window.
   *
   * @param mat            the material to edit (modified in place)
   * @param labelWidth     ImTable label-column widths (e.g. {89_px, -1.0f})
   * @param includeTextures also draw the Texture 0/1 sub-sections
   * @param usePlaceholder  in the texture sub-sections, expose the dynamic-placeholder
   *                        combo (model materials). When false, a plain texture picker is
   *                        shown — used by emitters, which bind textures directly.
   */
  inline void drawMaterialSettings(
    Project::Assets::Material &mat,
    const ImVec2 &labelWidth,
    bool includeTextures = true,
    bool usePlaceholder = true)
  {
    auto subSection = [&labelWidth](const char* name, auto cb)
    {
      if(ImGui::CollapsingSubHeader(name, ImGuiTreeNodeFlags_DefaultOpen) && ImTable::start(name, nullptr, labelWidth))
      {
        cb();
        ImTable::end();
      }
    };

    auto usage = N64::CC::getUsage(mat.cc.value);
    // enforce disabling unused values
    if(!usage.prim)mat.primColorSet.value = false;
    if(!usage.env)mat.envColorSet.value = false;
    if(!usage.k4k5)mat.k4k5Set.value = false;
    if(!usage.lod)mat.primLodSet.value = false;

    subSection("Color-Combiner", [&]
    {
      ImTable::add("2-Cycle");

      glm::ivec4 cc[2], cca[2];
      N64::CC::unpackCC(mat.cc.value, cc[0], cca[0], cc[1], cca[1]);

      if(ImGui::Checkbox("##2C", &usage.twoCycle) && usage.twoCycle)
      {
        // if we enable 2-cycle mode, force a pass-through by default
        cc[1][0] = N64::CC::NAMES_COL_A.size() - 1;
        cc[1][1] = N64::CC::NAMES_COL_B.size() - 1;
        cc[1][2] = N64::CC::NAMES_COL_C.size() - 1;
        cc[1][3] = 0;

        cca[1][0] = N64::CC::NAMES_ALPHA_A.size() - 1;
        cca[1][1] = N64::CC::NAMES_ALPHA_B.size() - 1;
        cca[1][2] = N64::CC::NAMES_ALPHA_C.size() - 1;
        cca[1][3] = 0;
      }

      for(int c = 0; c < (usage.twoCycle ? 2 : 1); ++c)
      {
        ImGui::PushID(c);
        ImTable::add("A");
        ImGui::SideBySide(
          [&]{ ImGui::Combo("##C0C_A",  &cc[c][0], N64::CC::NAMES_COL_A.data(), N64::CC::NAMES_COL_A.size()); },
          [&]{ ImGui::Combo("##C0A_A", &cca[c][0], N64::CC::NAMES_ALPHA_A.data(), N64::CC::NAMES_ALPHA_A.size()); }
        );
        ImTable::add("B");
        ImGui::SideBySide(
          [&]{ ImGui::Combo("##C0C_B",  &cc[c][1], N64::CC::NAMES_COL_B.data(), N64::CC::NAMES_COL_B.size()); },
          [&]{ ImGui::Combo("##C0A_B", &cca[c][1], N64::CC::NAMES_ALPHA_B.data(), N64::CC::NAMES_ALPHA_B.size()); }
        );
        ImTable::add("C");
        ImGui::SideBySide(
          [&]{ ImGui::Combo("##C0C_C",  &cc[c][2], N64::CC::NAMES_COL_C.data(), N64::CC::NAMES_COL_C.size()); },
          [&]{ ImGui::Combo("##C0A_C", &cca[c][2], N64::CC::NAMES_ALPHA_C.data(), N64::CC::NAMES_ALPHA_C.size()); }
        );
        ImTable::add("D");
        ImGui::SideBySide(
          [&]{ ImGui::Combo("##C0C_D",  &cc[c][3], N64::CC::NAMES_COL_D.data(), N64::CC::NAMES_COL_D.size()); },
          [&]{ ImGui::Combo("##C0A_D", &cca[c][3], N64::CC::NAMES_ALPHA_D.data(), N64::CC::NAMES_ALPHA_D.size()); }
        );
        ImGui::PopID();

        ImTable::add("Color");
        printCC(
          N64::CC::NAMES_COL_A[cc[c][0]], N64::CC::NAMES_COL_B[cc[c][1]],
          N64::CC::NAMES_COL_C[cc[c][2]], N64::CC::NAMES_COL_D[cc[c][3]]
        );
        ImTable::add("Alpha");
        printCC(
          N64::CC::NAMES_ALPHA_A[cca[c][0]], N64::CC::NAMES_ALPHA_B[cca[c][1]],
          N64::CC::NAMES_ALPHA_C[cca[c][2]], N64::CC::NAMES_ALPHA_D[cca[c][3]]
       );

        if(usage.twoCycle && c == 0) {
          ImGui::Dummy({0, 4_px});
        }
      }

      if(!usage.twoCycle) {
        cc[1] = cc[0];
        cca[1] = cca[0];
      }

      mat.cc.value = N64::CC::packCC(cc[0], cca[0], cc[1], cca[1]);
      if(usage.twoCycle) {
        mat.cc.value |= RDPQ_COMBINER_2PASS;
      }
    });

    mat.tex0.set.value = usage.tex0;
    mat.tex1.set.value = usage.tex1;

    if(includeTextures)
    {
      auto drawMatTex = [&](Project::Assets::MaterialTex &tex, uint32_t id) {
        ImGui::PushID(id + 0xFF);

        if(usePlaceholder) {
          ImTable::add("Placeholder");
          ImGui::Combo("##PH", &tex.dynType.value, "None\0" "Tile\0" "Texture + Tile\0");

          if(tex.dynType.value == tex.DYN_TYPE_FULL) {
            ImTable::addProp("Size", tex.texSize);
          } else {
            TextureEditor::draw(tex);
          }
        } else {
          TextureEditor::draw(tex);
        }

        ImGui::PopID();
      };

      if(usage.tex0)subSection("Texture 0", [&]{ drawMatTex(mat.tex0, 0); });
      if(usage.tex1)subSection("Texture 1", [&]{ drawMatTex(mat.tex1, 1); });
    }

    subSection("Sampling", [&]
    {
      toggleProp("Perspect.", mat.perspSet.value, mat.persp);

      toggleProp("Dither", mat.ditherSet.value, [&] {
        ImGui::Combo("##Dither", &mat.dither.value, DITHER_MODES);
      });

      toggleProp("Filtering", mat.filterSet.value, [&] {
        int val = mat.filter.value == 0 ? 0 : 1; // map 2->1
        ImGui::Combo("##", &val, "Nearest\0Bilinear\0");
        mat.filter.value = val == 0 ? 0 : 2;
      });
    });

    if(usage.prim || usage.env || usage.lod || usage.k4k5)
    {
      subSection("Values", [&]
      {
        if(usage.prim)toggleProp("Prim", mat.primColorSet.value, mat.primColor);
        if(usage.env)toggleProp("Env", mat.envColorSet.value, mat.envColor);
        if(usage.lod)toggleProp("LOD", mat.primLodSet.value, mat.primLod);
        if(usage.k4k5)toggleProp("K4/K5", mat.k4k5Set.value, mat.k4k5);

        mat.primLod.value = glm::clamp(mat.primLod.value, 0u, 255u);
        mat.k4k5.value = glm::clamp(mat.k4k5.value, 0, 255);
      });
    }

    subSection("Geometry Modes", [&]
    {
      ImTable::add("Vertex FX");
      ImGui::Combo("##Vert", &mat.vertexFX.value, VERTEX_EFFECTS);

      ImTable::add("Unlit");
      ImGui::CheckboxFlags("##Unlit", &mat.drawFlags.value, T3D::FLAG_NO_LIGHT);

      ImTable::addProp("Fog to Alpha", mat.fogToAlpha);

      ImTable::add("Cull-Front");
      ImGui::CheckboxFlags("##CF", &mat.drawFlags.value, T3D::FLAG_CULL_FRONT);
      ImTable::add("Cull-Back");
      ImGui::CheckboxFlags("##CB", &mat.drawFlags.value, T3D::FLAG_CULL_BACK);
    });

    subSection("Render Modes", [&]
    {
      toggleProp("Alpha-Clip", mat.alphaCompSet.value, [&] {
        ImGui::SliderInt("##AC", &mat.alphaComp.value, 0, 255,
          mat.alphaComp.value == 0 ? "<Off>" : "%d"
        );
      });

      toggleProp("Depth", mat.zmodeSet.value, [&] {
        ImGui::Combo("##", &mat.zmode.value, Z_MODES);
      });

      toggleProp("Anti-Alias", mat.aaSet.value, [&] {
        ImGui::Combo("##AA", &mat.aa.value, AA_MODES);
      });

      toggleProp("Blending", mat.blenderSet.value, [&]
      {
        std::vector<ImTable::ComboEntry> blenders{
          {0, "None (Opaque)"},
          {RDPQ_BLENDER_MULTIPLY, "Multiply (Alpha)"},
          {RDPQ_BLENDER_ADDITIVE, "Additive"},
        };
        ImTable::addVecComboBox("", blenders, mat.blender.value);
      });

      toggleProp("Fog", mat.fogSet.value, [&]
      {
        std::vector<ImTable::ComboEntry> fogs{
          {0, "None"},
          {RDPQ_FOG_STANDARD, "Fog (Standard)"},
        };
        ImTable::addVecComboBox("", fogs, mat.fog.value);
      });

      toggleProp("Fixed-Z", mat.zprimSet.value, [&] {
        ImGui::SideBySide(
          [&]{ ImGui::InputInt("##0", &mat.zprim.value); },
          [&]{ ImGui::InputInt("##1", &mat.zdelta.value); }
        );
      });
    });
  }
}
