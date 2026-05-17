#pragma once

#ifndef ES_APP_SCRAPERS_HOW_LONG_TO_BEAT_SCRAPER_H
#define ES_APP_SCRAPERS_HOW_LONG_TO_BEAT_SCRAPER_H

#include "scrapers/Scraper.h"

class HowLongToBeatScraper : public Scraper
{
public:
	void generateRequests(const ScraperSearchParams& params,
		std::queue<std::unique_ptr<ScraperRequest>>& requests,
		std::vector<ScraperSearchResult>& results) override;

	bool isSupportedPlatform(SystemData* system) override;

	const std::set<ScraperMediaSource>& getSupportedMedias() override;
};

class HowLongToBeatRequest : public ScraperRequest
{
public:
	HowLongToBeatRequest(std::vector<ScraperSearchResult>& resultsWrite, FileData* game, const std::string& searchName);

	void update() override;

private:
	enum class Stage
	{
		Init,
		Search
	};

	void startInit();
	void startSearch();
	bool processInit(const std::string& response);
	void processSearch(const std::string& response);
	std::string buildSearchBody() const;

	FileData* mGame;
	std::string mSearchName;
	std::string mToken;
	std::string mHpKey;
	std::string mHpVal;
	Stage mStage;
	std::unique_ptr<HttpReq> mRequest;
};

#endif // ES_APP_SCRAPERS_HOW_LONG_TO_BEAT_SCRAPER_H
