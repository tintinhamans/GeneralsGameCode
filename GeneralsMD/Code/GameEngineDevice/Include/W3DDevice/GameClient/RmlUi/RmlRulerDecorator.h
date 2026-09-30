/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// FILE: RmlRulerDecorator.h /////////////////////////////////////////////////////
// The screen frame's tick scale, drawn as geometry: decorator: ruler-ticks(<edge> <color>).
// Ticks stand on <edge> of the element and point into it: a minor tick every 4dp, a longer one
// every 5th and the longest every 10th, over a faint scrim that fades out towards the far side.
// The scale fills whatever box it is given, so it reaches both ends of an edge at any resolution
// and aspect ratio, and every tick snaps to whole pixels so it stays crisp at any dp ratio. No
// texture is involved (the engine's RmlUi textures are clamped, so nothing could repeat).
// Header-only so the rmlui_render harness registers the very same decorator.

#pragma once

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Decorator.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/RenderManager.h>

#include <algorithm>
#include <cmath>

class RmlRulerTicksDecorator : public Rml::Decorator
{
public:
	enum class Edge { Top, Bottom, Left, Right };

	RmlRulerTicksDecorator(Edge edge, Rml::Colourb tick) : m_edge(edge), m_tick(tick) {}

	Rml::DecoratorDataHandle GenerateElementData(Rml::Element *element, Rml::BoxArea paintArea) const override
	{
		Rml::RenderManager *renderManager = element->GetRenderManager();
		if (!renderManager)
			return INVALID_DECORATORDATAHANDLE;

		const Rml::RenderBox box = element->GetRenderBox(paintArea);
		const Rml::Vector2f origin = box.GetFillOffset();
		const Rml::Vector2f size = box.GetFillSize();
		const bool horizontal = m_edge == Edge::Top || m_edge == Edge::Bottom;
		const bool farSide = m_edge == Edge::Bottom || m_edge == Edge::Right;
		const float along = horizontal ? size.x : size.y;
		const float depth = horizontal ? size.y : size.x;
		if (along <= 0.0f || depth <= 0.0f)
			return INVALID_DECORATORDATAHANDLE;

		const float opacity = element->GetComputedValues().opacity();
		const float dp = element->GetContext() ? element->GetContext()->GetDensityIndependentPixelRatio() : 1.0f;
		const float thickness = std::max(1.0f, std::floor(dp + 0.5f));

		Rml::Mesh mesh;

		// Scrim: darkest on the edge, gone at the far side of the box, and faded in over the first and
		// last `depth` of the run so a scale that ends short of a corner leaves no hard step.
		{
			const float ramp = std::min(depth, along * 0.5f);
			const float alongStops[4] = {0.0f, ramp, along - ramp, along};
			const float alongFade[4] = {0.0f, 1.0f, 1.0f, 0.0f};
			const int base = (int)mesh.vertices.size();
			for (int a = 0; a < 4; ++a)
			{
				for (int d = 0; d < 2; ++d)
				{
					// d == 0 is the base of the ticks (the edge), d == 1 the far side.
					const float across = (farSide ? (d == 0 ? depth : 0.0f) : (d == 0 ? 0.0f : depth));
					const float alpha = kScrimAlpha * alongFade[a] * (d == 0 ? 1.0f : 0.0f);
					const Rml::ColourbPremultiplied c =
						Rml::Colourb(5, 7, 15, (Rml::byte)std::floor(alpha + 0.5f)).ToPremultiplied(opacity);
					const Rml::Vector2f p = horizontal ? Rml::Vector2f(origin.x + alongStops[a], origin.y + across)
					                                   : Rml::Vector2f(origin.x + across, origin.y + alongStops[a]);
					mesh.vertices.push_back(Rml::Vertex{p, c, {0, 0}});
				}
			}
			for (int a = 0; a < 3; ++a)
			{
				const int v = base + a * 2;
				for (int k : {0, 1, 3, 0, 3, 2})
					mesh.indices.push_back(v + k);
			}
		}

		const Rml::ColourbPremultiplied tickColour = m_tick.ToPremultiplied(opacity);
		const float unit = kUnitDp * dp;
		for (int i = 0;; ++i)
		{
			const float pos = std::floor(i * unit + 0.5f);
			if (pos >= along)
				break;
			const float lenDp = i % 10 == 0 ? kMajorDp : (i % 5 == 0 ? kMidDp : kMinorDp);
			const float len = std::min(depth, std::floor(lenDp * dp + 0.5f));
			const float across = std::min(thickness, along - pos);

			Rml::Vector2f tickOrigin, tickSize;
			if (horizontal)
			{
				tickOrigin = Rml::Vector2f(origin.x + pos, origin.y + (farSide ? depth - len : 0.0f));
				tickSize = Rml::Vector2f(across, len);
			}
			else
			{
				tickOrigin = Rml::Vector2f(origin.x + (farSide ? depth - len : 0.0f), origin.y + pos);
				tickSize = Rml::Vector2f(len, across);
			}
			Rml::MeshUtilities::GenerateQuad(mesh, tickOrigin, tickSize, tickColour);
		}

		Rml::Geometry *geometry = new Rml::Geometry(renderManager->MakeGeometry(std::move(mesh)));
		return reinterpret_cast<Rml::DecoratorDataHandle>(geometry);
	}

	void ReleaseElementData(Rml::DecoratorDataHandle data) const override { delete reinterpret_cast<Rml::Geometry *>(data); }

	void RenderElement(Rml::Element *element, Rml::DecoratorDataHandle data) const override
	{
		reinterpret_cast<Rml::Geometry *>(data)->Render(element->GetAbsoluteOffset(Rml::BoxArea::Border));
	}

private:
	static constexpr float kUnitDp = 4.0f;
	static constexpr float kMinorDp = 5.0f;
	static constexpr float kMidDp = 9.0f;
	static constexpr float kMajorDp = 14.0f;
	static constexpr float kScrimAlpha = 56.0f;

	Edge m_edge;
	Rml::Colourb m_tick;
};

class RmlRulerTicksInstancer : public Rml::DecoratorInstancer
{
public:
	RmlRulerTicksInstancer()
	{
		m_edgeId = RegisterProperty("edge", "top").AddParser("keyword", "top, bottom, left, right").GetId();
		m_colorId = RegisterProperty("color", "#8E97F8").AddParser("color").GetId();
		RegisterShorthand("decorator", "edge, color", Rml::ShorthandType::FallThrough);
	}

	Rml::SharedPtr<Rml::Decorator> InstanceDecorator(const Rml::String &, const Rml::PropertyDictionary &properties,
		const Rml::DecoratorInstancerInterface &) override
	{
		const Rml::Property *edge = properties.GetProperty(m_edgeId);
		const Rml::Property *color = properties.GetProperty(m_colorId);
		if (!edge || !color)
			return nullptr;
		return Rml::MakeShared<RmlRulerTicksDecorator>(static_cast<RmlRulerTicksDecorator::Edge>(edge->Get<int>()), color->Get<Rml::Colourb>());
	}

	// Registers "ruler-ticks"; the instancer must outlive the RmlUi core.
	static void registerWithRml()
	{
		static RmlRulerTicksInstancer instancer;
		Rml::Factory::RegisterDecoratorInstancer("ruler-ticks", &instancer);
	}

private:
	Rml::PropertyId m_edgeId;
	Rml::PropertyId m_colorId;
};
