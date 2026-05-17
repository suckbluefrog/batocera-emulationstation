#include "scrapers/MixedScraper.h"

#include "FileData.h"
#include "Log.h"
#include "Settings.h"
#include "SystemData.h"
#include "utils/StringUtil.h"

#include <algorithm>
#include <vector>

namespace
{
	const std::string DEFAULT_METADATA_PRIORITY = "LocalLaunchers;ScreenScraper;HfsDB;IGDB;HowLongToBeat;ProtonDB;PCGamingWiki;ArcadeDB;TheGamesDB";
	const std::string DEFAULT_MEDIA_PRIORITY = "SteamGridDB;ScreenScraper;HfsDB;ArcadeDB;IGDB;TheGamesDB";

	std::vector<std::string> getPriorityList(const std::string& settingName, const std::string& defaultValue)
	{
		auto value = Settings::getInstance()->getString(settingName);
		if (value.empty())
			value = defaultValue;

		std::vector<std::string> result;
		for (auto item : Utils::String::split(value, ';', true))
		{
			item = Utils::String::trim(item);
			if (item.empty() || item == "Mixed")
				continue;
			if (std::find(result.cbegin(), result.cend(), item) == result.cend())
				result.push_back(item);
		}

		return result;
	}

	std::vector<std::string> combineOrder(const std::vector<std::string>& metadataOrder, const std::vector<std::string>& mediaOrder)
	{
		std::vector<std::string> result;
		for (auto order : { metadataOrder, mediaOrder })
			for (auto item : order)
				if (std::find(result.cbegin(), result.cend(), item) == result.cend())
					result.push_back(item);

		return result;
	}

	bool isMediaMetadata(MetaDataId id)
	{
		return id == MetaDataId::Image || id == MetaDataId::Thumbnail || id == MetaDataId::Marquee ||
			id == MetaDataId::Video || id == MetaDataId::FanArt || id == MetaDataId::TitleShot ||
			id == MetaDataId::Manual || id == MetaDataId::Magazine || id == MetaDataId::Map ||
			id == MetaDataId::Bezel || id == MetaDataId::Cartridge || id == MetaDataId::BoxArt ||
			id == MetaDataId::Wheel || id == MetaDataId::Mix || id == MetaDataId::BoxBack ||
			id == MetaDataId::LaunchVideo;
	}

	bool isHltbMetadata(MetaDataId id)
	{
		return id == MetaDataId::HltbId || id == MetaDataId::HltbMainTime || id == MetaDataId::HltbExtraTime ||
			id == MetaDataId::HltbCompletionistTime || id == MetaDataId::HltbAllStylesTime || id == MetaDataId::HltbUrl;
	}

	bool isProtonDBMetadata(MetaDataId id)
	{
		return id == MetaDataId::SteamAppId || id == MetaDataId::ProtonDBTier || id == MetaDataId::ProtonDBConfidence ||
			id == MetaDataId::ProtonDBScore || id == MetaDataId::ProtonDBTotal || id == MetaDataId::ProtonDBTrendingTier ||
			id == MetaDataId::ProtonDBUrl;
	}

	bool isPCGamingWikiMetadata(MetaDataId id)
	{
		return id == MetaDataId::PCGamingWikiPage || id == MetaDataId::PCGamingWikiPageId || id == MetaDataId::PCGamingWikiUrl ||
			id == MetaDataId::PCGamingWikiDevelopers || id == MetaDataId::PCGamingWikiPublishers;
	}

	bool isLauncherMetadata(MetaDataId id)
	{
		return id == MetaDataId::LauncherSource || id == MetaDataId::LauncherStore || id == MetaDataId::LauncherId ||
			id == MetaDataId::LauncherInstallPath || id == MetaDataId::LauncherExecutable || id == MetaDataId::LauncherRunner ||
			id == MetaDataId::LauncherWinePrefix || id == MetaDataId::LauncherWineVersion || id == MetaDataId::LauncherStoreUrl ||
			id == MetaDataId::LauncherCloudSave || id == MetaDataId::EpicNamespace || id == MetaDataId::GogId;
	}

	bool isStandardMetadata(MetaDataId id)
	{
		return id == MetaDataId::Name || id == MetaDataId::Desc || id == MetaDataId::Genre ||
			id == MetaDataId::Rating || id == MetaDataId::ReleaseDate || id == MetaDataId::Developer ||
			id == MetaDataId::Publisher || id == MetaDataId::Family || id == MetaDataId::ArcadeSystemName ||
			id == MetaDataId::Players || id == MetaDataId::Language || id == MetaDataId::Region ||
			id == MetaDataId::ScraperId;
	}

	bool shouldMergeMetadata(const std::string& scraperName, MetaDataId id)
	{
		if (scraperName == "HowLongToBeat")
			return isHltbMetadata(id);
		if (scraperName == "ProtonDB")
			return isProtonDBMetadata(id);
		if (scraperName == "PCGamingWiki")
			return isPCGamingWikiMetadata(id);
		if (scraperName == "LocalLaunchers")
			return isLauncherMetadata(id) || id == MetaDataId::Name || id == MetaDataId::Desc || id == MetaDataId::Developer || id == MetaDataId::Publisher;
		if (scraperName == "SteamGridDB")
			return false;

		return isStandardMetadata(id);
	}

	bool hasUsableValue(MetaDataId id, const std::string& value)
	{
		if (value.empty() || value == "not-a-date-time")
			return false;

		if (id == MetaDataId::Rating)
			return Utils::String::toFloat(value) > 0;

		if (id == MetaDataId::HltbId || id == MetaDataId::HltbMainTime || id == MetaDataId::HltbExtraTime ||
			id == MetaDataId::HltbCompletionistTime || id == MetaDataId::HltbAllStylesTime || id == MetaDataId::SteamAppId ||
			id == MetaDataId::ProtonDBTotal)
			return Utils::String::toInteger(value) > 0;

		return value != "0";
	}

	std::string getPCGamingWikiDescription(const MetaDataList& metadata)
	{
		auto page = metadata.get(MetaDataId::PCGamingWikiPage);
		if (page.empty())
			return "";

		std::vector<std::string> lines;
		lines.push_back("PCGamingWiki: " + page);

		auto developers = metadata.get(MetaDataId::PCGamingWikiDevelopers);
		if (!developers.empty())
			lines.push_back("Developers: " + developers);

		auto publishers = metadata.get(MetaDataId::PCGamingWikiPublishers);
		if (!publishers.empty())
			lines.push_back("Publishers: " + publishers);

		auto url = metadata.get(MetaDataId::PCGamingWikiUrl);
		if (!url.empty())
			lines.push_back("URL: " + url);

		return Utils::String::join(lines, "\n");
	}

	std::string appendPCGamingWikiDescription(const std::string& description, const std::string& pcgwDescription)
	{
		if (pcgwDescription.empty() || description.find(pcgwDescription) != std::string::npos)
			return description;

		auto trimmed = Utils::String::trim(description);
		if (trimmed.empty())
			return pcgwDescription;

		return trimmed + "\n\n" + pcgwDescription;
	}
}

void MixedScraper::generateRequests(const ScraperSearchParams& params,
	std::queue<std::unique_ptr<ScraperRequest>>& requests,
	std::vector<ScraperSearchResult>& results)
{
	requests.push(std::unique_ptr<ScraperRequest>(new MixedScraperRequest(results, params)));
}

bool MixedScraper::isSupportedPlatform(SystemData* system)
{
	return system != nullptr;
}

const std::set<Scraper::ScraperMediaSource>& MixedScraper::getSupportedMedias()
{
	static std::set<ScraperMediaSource> mdds =
	{
		ScraperMediaSource::Screenshot,
		ScraperMediaSource::Box2d,
		ScraperMediaSource::Box3d,
		ScraperMediaSource::Mix,
		ScraperMediaSource::Marquee,
		ScraperMediaSource::Wheel,
		ScraperMediaSource::TitleShot,
		ScraperMediaSource::Video,
		ScraperMediaSource::FanArt,
		ScraperMediaSource::Manual,
		ScraperMediaSource::Map,
		ScraperMediaSource::BoxBack,
		ScraperMediaSource::Bezel_16_9
	};

	return mdds;
}

MixedScraperRequest::MixedScraperRequest(std::vector<ScraperSearchResult>& resultsWrite, const ScraperSearchParams& params)
	: ScraperRequest(resultsWrite),
	mParams(params),
	mMerged("Mixed")
{
	mMetadataOrder = getPriorityList("MixedScraperMetadataPriority", DEFAULT_METADATA_PRIORITY);
	mMediaOrder = getPriorityList("MixedScraperMediaPriority", DEFAULT_MEDIA_PRIORITY);
	mCombinedOrder = combineOrder(mMetadataOrder, mMediaOrder);
	mIndex = 0;
	mChanged = false;

	if (mParams.game != nullptr)
		mMerged.mdl = mParams.game->getMetadata();

	setStatus(ASYNC_IN_PROGRESS);
	startNext();
}

void MixedScraperRequest::startNext()
{
	mCurrent.reset();

	while (mIndex < mCombinedOrder.size())
	{
		auto scraperName = mCombinedOrder[mIndex++];
		auto scraper = Scraper::getScraper(scraperName);
		if (scraper == nullptr || !scraper->isSupportedPlatform(mParams.system))
			continue;

		try
		{
			mCurrent = scraper->search(mParams);
		}
		catch (std::exception& e)
		{
			LOG(LogWarning) << "MixedScraper skipped " << scraperName << ": " << e.what();
			mCurrent.reset();
			continue;
		}

		return;
	}

	finish();
}

void MixedScraperRequest::update()
{
	if (!mCurrent)
		return;

	mCurrent->update();
	if (mCurrent->status() == ASYNC_IN_PROGRESS)
		return;

	auto scraperName = mCombinedOrder[mIndex - 1];
	if (mCurrent->status() == ASYNC_DONE)
	{
		auto results = mCurrent->getResults();
		if (!results.empty())
			mChildResults[scraperName] = results[0];
	}
	else
		LOG(LogWarning) << "MixedScraper child failed " << scraperName << ": " << mCurrent->getStatusString();

	startNext();
}

void MixedScraperRequest::mergeMetadata(const std::string& scraperName, const ScraperSearchResult& result)
{
	for (auto mdd : MetaDataList::getMDD())
	{
		if (isMediaMetadata(mdd.id) || !shouldMergeMetadata(scraperName, mdd.id))
			continue;

		auto value = result.mdl.get(mdd.id);
		if (!hasUsableValue(mdd.id, value))
			continue;

		if (mFilledMetadata.find(mdd.id) != mFilledMetadata.cend())
			continue;

		mMerged.mdl.set(mdd.id, value);
		mFilledMetadata.insert(mdd.id);
		mChanged = true;
	}
}

void MixedScraperRequest::mergeMedia(const ScraperSearchResult& result)
{
	for (auto url : result.urls)
	{
		if (url.second.url.empty() || mFilledMedia.find(url.first) != mFilledMedia.cend())
			continue;

		mMerged.urls[url.first] = url.second;
		mFilledMedia.insert(url.first);
		mChanged = true;
	}
}

void MixedScraperRequest::finish()
{
	for (auto scraperName : mMetadataOrder)
	{
		auto it = mChildResults.find(scraperName);
		if (it != mChildResults.cend())
			mergeMetadata(scraperName, it->second);
	}

	auto pcgwIt = mChildResults.find("PCGamingWiki");
	if (pcgwIt != mChildResults.cend())
	{
		auto pcgwDescription = getPCGamingWikiDescription(pcgwIt->second.mdl);
		auto mergedDescription = appendPCGamingWikiDescription(mMerged.mdl.get(MetaDataId::Desc), pcgwDescription);
		if (mergedDescription != mMerged.mdl.get(MetaDataId::Desc))
		{
			mMerged.mdl.set(MetaDataId::Desc, mergedDescription);
			mChanged = true;
		}
	}

	for (auto scraperName : mMediaOrder)
	{
		auto it = mChildResults.find(scraperName);
		if (it != mChildResults.cend())
			mergeMedia(it->second);
	}

	if (mChanged)
		mResults.push_back(mMerged);

	setStatus(ASYNC_DONE);
}
