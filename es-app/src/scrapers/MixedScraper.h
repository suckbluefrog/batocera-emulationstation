#pragma once

#ifndef ES_APP_SCRAPERS_MIXED_SCRAPER_H
#define ES_APP_SCRAPERS_MIXED_SCRAPER_H

#include "scrapers/Scraper.h"

class MixedScraper : public Scraper
{
public:
	void generateRequests(const ScraperSearchParams& params,
		std::queue<std::unique_ptr<ScraperRequest>>& requests,
		std::vector<ScraperSearchResult>& results) override;

	bool isSupportedPlatform(SystemData* system) override;

	const std::set<ScraperMediaSource>& getSupportedMedias() override;
};

class MixedScraperRequest : public ScraperRequest
{
public:
	MixedScraperRequest(std::vector<ScraperSearchResult>& resultsWrite, const ScraperSearchParams& params);

	void update() override;

private:
	void startNext();
	void finish();
	void mergeMetadata(const std::string& scraperName, const ScraperSearchResult& result);
	void mergeMedia(const ScraperSearchResult& result);

	ScraperSearchParams mParams;
	std::vector<std::string> mCombinedOrder;
	std::vector<std::string> mMetadataOrder;
	std::vector<std::string> mMediaOrder;
	std::map<std::string, ScraperSearchResult> mChildResults;
	std::set<MetaDataId> mFilledMetadata;
	std::set<MetaDataId> mFilledMedia;
	std::unique_ptr<ScraperSearchHandle> mCurrent;
	ScraperSearchResult mMerged;
	unsigned int mIndex;
	bool mChanged;
};

#endif // ES_APP_SCRAPERS_MIXED_SCRAPER_H
