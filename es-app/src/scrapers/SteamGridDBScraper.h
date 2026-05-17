#pragma once

#ifndef ES_APP_SCRAPERS_STEAM_GRID_DB_SCRAPER_H
#define ES_APP_SCRAPERS_STEAM_GRID_DB_SCRAPER_H

#include "scrapers/Scraper.h"

class SteamGridDBScraper : public Scraper
{
public:
	void generateRequests(const ScraperSearchParams& params,
		std::queue<std::unique_ptr<ScraperRequest>>& requests,
		std::vector<ScraperSearchResult>& results) override;

	bool isSupportedPlatform(SystemData* system) override;

	const std::set<ScraperMediaSource>& getSupportedMedias() override;
};

class SteamGridDBRequest : public ScraperHttpRequest
{
public:
	enum LookupMode
	{
		SearchByName,
		SteamAppId
	};

	SteamGridDBRequest(std::vector<ScraperSearchResult>& resultsWrite,
		const std::string& url,
		HttpReqOptions* options,
		LookupMode lookupMode,
		bool isManualScrape)
		: ScraperHttpRequest(resultsWrite, url, options)
	{
		mLookupMode = lookupMode;
		mIsManualScrape = isManualScrape;
	}

	virtual bool retryOn249() { return !mIsManualScrape; }

protected:
	void preProcess(const std::string& response) override;
	bool process(const std::string& response, std::vector<ScraperSearchResult>& results) override;

private:
	struct Game
	{
		int id = 0;
		std::string name;
	};

	std::vector<Game> parseGames(const std::string& response);
	std::vector<std::string> getAssetUrls(const std::string& dependencyId);

	LookupMode mLookupMode;
	bool mIsManualScrape;
	std::vector<Game> mGames;
};

#endif // ES_APP_SCRAPERS_STEAM_GRID_DB_SCRAPER_H
