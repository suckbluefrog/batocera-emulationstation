#pragma once

#ifndef ES_APP_SCRAPERS_PROTON_DB_SCRAPER_H
#define ES_APP_SCRAPERS_PROTON_DB_SCRAPER_H

#include "scrapers/Scraper.h"

class ProtonDBScraper : public Scraper
{
public:
	void generateRequests(const ScraperSearchParams& params,
		std::queue<std::unique_ptr<ScraperRequest>>& requests,
		std::vector<ScraperSearchResult>& results) override;

	bool isSupportedPlatform(SystemData* system) override;

	const std::set<ScraperMediaSource>& getSupportedMedias() override;
};

class ProtonDBRequest : public ScraperHttpRequest
{
public:
	ProtonDBRequest(std::vector<ScraperSearchResult>& resultsWrite,
		FileData* game,
		const std::string& appId,
		const std::string& url)
		: ScraperHttpRequest(resultsWrite, url)
	{
		mGame = game;
		mAppId = appId;
	}

protected:
	bool process(const std::string& response, std::vector<ScraperSearchResult>& results) override;

private:
	FileData* mGame;
	std::string mAppId;
};

#endif // ES_APP_SCRAPERS_PROTON_DB_SCRAPER_H
