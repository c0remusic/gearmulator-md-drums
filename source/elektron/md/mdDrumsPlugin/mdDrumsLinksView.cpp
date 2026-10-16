#include "mdDrumsLinksView.h"

#include "mdDrumsController.h"
#include "mdDrumsDom.h"
#include "mdDrumsEditor.h"
#include "mdDrumsFrame.h"
#include "mdDrumsTrackView.h"

#include "RmlUi/Core/Element.h"

namespace mdDrums
{
	LinksView::LinksView(Editor& _editor) : m_editor(_editor)
	{
		const auto toggle = [this](Rml::Event&)
		{
			auto* tracks = m_editor.getTrackView();
			if(!tracks)
				return;
			const bool open = tracks->getOverlay() == TrackView::Overlay::Links;
			tracks->setOverlay(open ? TrackView::Overlay::None : TrackView::Overlay::Links);
		};
		m_editor.addClick("link_word", toggle);
		m_editor.addClick("choke_word", toggle);
		m_editor.addClick("linksmenu_done", [this](Rml::Event&)
		{
			if(auto* tracks = m_editor.getTrackView())
				tracks->setOverlay(TrackView::Overlay::None);
		});
		m_editor.addClick("links_link_off", [this](Rml::Event&) { choose(false, Controller::Off); });
		m_editor.addClick("links_choke_off", [this](Rml::Event&) { choose(true, Controller::Off); });
		for(uint8_t t = 0; t < Controller::TrackCount; ++t)
		{
			const auto n = std::to_string(t + 1);
			m_editor.addClick("links_link" + n, [this, t](Rml::Event&) { choose(false, t); });
			m_editor.addClick("links_choke" + n, [this, t](Rml::Event&) { choose(true, t); });
		}
	}

	LinksView::~LinksView() = default;

	Rml::Element* LinksView::find(const std::string& _id) const
	{
		return m_editor.element(_id);
	}

	bool LinksView::step(const Frame& _frame)
	{
		const auto& controller = m_editor.controller();
		const auto track = _frame.shown;
		if(track == m_shownTrack && controller.link(track) == m_shownLink && controller.choke(track) == m_shownChoke)
			return false;
		refresh(track);
		return true;
	}

	void LinksView::choose(const bool _choke, const uint8_t _target)
	{
		// The Track the menu shows
		if(m_shownTrack < 0)
			return;
		const auto track = static_cast<uint8_t>(m_shownTrack);
		// The Track itself does nothing on the Machinedrum: its cell is inert
		if(_target == track)
			return;
		auto& controller = m_editor.controller();
		if(_choke)
			controller.setChoke(track, _target);
		else
			controller.setLink(track, _target);
		refresh(track);
	}

	void LinksView::refresh(const uint8_t _track)
	{
		const auto& controller = m_editor.controller();
		const auto track = _track;
		const auto link = controller.link(track);
		const auto choke = controller.choke(track);
		m_shownTrack = track;
		m_shownLink = link;
		m_shownChoke = choke;

		const bool linked = link < Controller::TrackCount;
		const bool choking = choke < Controller::TrackCount;
		setText(find("link_text"), linked ? "Also strikes <b>track " + twoDigits(link + 1) + "</b>" : std::string("Strikes no other track"));
		setText(find("choke_text"), choking ? "Silences <b>track " + twoDigits(choke + 1) + "</b>" : std::string("Silences no track"));
		setText(find("linksmenu_label"), "Track " + twoDigits(track + 1));

		if(auto* off = find("links_link_off"))
			off->SetClass("on", !linked);
		if(auto* off = find("links_choke_off"))
			off->SetClass("on", !choking);
		for(uint8_t t = 0; t < Controller::TrackCount; ++t)
		{
			const auto n = std::to_string(t + 1);
			if(auto* cell = find("links_link" + n))
			{
				cell->SetClass("on", t == link);
				cell->SetClass("self", t == track);
			}
			if(auto* cell = find("links_choke" + n))
			{
				cell->SetClass("on", t == choke);
				cell->SetClass("self", t == track);
			}
		}
	}
}
