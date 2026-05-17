#pragma once

#ifndef ES_APP_SCRAPERS_PC_GAMING_WIKI_SCRAPER_H
#define ES_APP_SCRAPERS_PC_GAMING_WIKI_SCRAPER_H

#include "scrapers/Scraper.h"

class PCGamingWikiScraper : public Scraper
{
public:
	void generateRequests(const ScraperSearchParams& params,
		std::queue<std::unique_ptr<ScraperRequest>>& requests,
		std::vector<ScraperSearchResult>& results) override;

	bool isSupportedPlatform(SystemData* system) override;

	const std::set<ScraperMediaSource>& getSupportedMedias() override;
};

class PCGamingWikiRequest : public ScraperHttpRequest
{
public:
	enum LookupMode
	{
		SteamAppId,
		NameSearch
	};

	PCGamingWikiRequest(std::vector<ScraperSearchResult>& resultsWrite,
		FileData* game,
		const std::string& url,
		LookupMode lookupMode)
		: ScraperHttpRequest(resultsWrite, url)
	{
		mGame = game;
		mLookupMode = lookupMode;
	}

protected:
	bool process(const std::string& response, std::vector<ScraperSearchResult>& results) override;

private:
	void addResult(std::vector<ScraperSearchResult>& results, const std::string& page, const std::string& pageId, const std::string& developers, const std::string& publishers);

	FileData* mGame;
	LookupMode mLookupMode;
};

#endif // ES_APP_SCRAPERS_PC_GAMING_WIKI_SCRAPER_H
