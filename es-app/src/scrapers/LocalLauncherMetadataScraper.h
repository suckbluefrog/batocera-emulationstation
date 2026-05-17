#pragma once

#ifndef ES_APP_SCRAPERS_LOCAL_LAUNCHER_METADATA_SCRAPER_H
#define ES_APP_SCRAPERS_LOCAL_LAUNCHER_METADATA_SCRAPER_H

#include "scrapers/Scraper.h"

class LocalLauncherMetadataScraper : public Scraper
{
public:
	void generateRequests(const ScraperSearchParams& params,
		std::queue<std::unique_ptr<ScraperRequest>>& requests,
		std::vector<ScraperSearchResult>& results) override;

	bool isSupportedPlatform(SystemData* system) override;

	const std::set<ScraperMediaSource>& getSupportedMedias() override;
};

class LocalLauncherMetadataRequest : public ScraperRequest
{
public:
	LocalLauncherMetadataRequest(std::vector<ScraperSearchResult>& resultsWrite, FileData* game, const std::string& searchName);

	void update() override;
};

#endif // ES_APP_SCRAPERS_LOCAL_LAUNCHER_METADATA_SCRAPER_H
