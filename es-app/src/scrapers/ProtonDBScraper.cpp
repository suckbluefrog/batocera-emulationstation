#include "scrapers/ProtonDBScraper.h"

#include "FileData.h"
#include "Log.h"
#include "SystemData.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

using namespace rapidjson;

namespace
{
	const std::string PROTONDB_SUMMARY_URL = "https://www.protondb.com/api/v1/reports/summaries/";
	const std::string PROTONDB_APP_URL = "https://www.protondb.com/app/";

	bool isDigits(const std::string& value)
	{
		return !value.empty() && std::all_of(value.cbegin(), value.cend(), [](char c) {
			return std::isdigit(static_cast<unsigned char>(c));
		});
	}

	std::string getSteamAppIdFromFile(FileData* game)
	{
		if (game == nullptr || Utils::String::toLower(Utils::FileSystem::getExtension(game->getPath())) != ".steam")
			return "";

		std::ifstream input(game->getPath());
		if (!input.is_open())
			return "";

		std::string line;
		while (std::getline(input, line))
		{
			line = Utils::String::trim(line);
			if (!Utils::String::startsWith(line, "appid="))
				continue;

			auto appId = Utils::String::trim(line.substr(6));
			if (isDigits(appId))
				return appId;
		}

		return "";
	}

	std::string getSteamAppId(FileData* game)
	{
		if (game == nullptr)
			return "";

		auto appId = game->getMetadata(MetaDataId::SteamAppId);
		if (isDigits(appId))
			return appId;

		return getSteamAppIdFromFile(game);
	}

	std::string getStringMember(const Value& value, const char* key)
	{
		if (!value.IsObject() || !value.HasMember(key) || !value[key].IsString())
			return "";

		return value[key].GetString();
	}
}

void ProtonDBScraper::generateRequests(const ScraperSearchParams& params,
	std::queue<std::unique_ptr<ScraperRequest>>& requests,
	std::vector<ScraperSearchResult>& results)
{
	auto appId = getSteamAppId(params.game);
	if (appId.empty())
		return;

	requests.push(std::unique_ptr<ScraperRequest>(new ProtonDBRequest(results, params.game, appId, PROTONDB_SUMMARY_URL + appId + ".json")));
}

bool ProtonDBScraper::isSupportedPlatform(SystemData* system)
{
	return system != nullptr;
}

const std::set<Scraper::ScraperMediaSource>& ProtonDBScraper::getSupportedMedias()
{
	static std::set<ScraperMediaSource> mdds = {};
	return mdds;
}

bool ProtonDBRequest::process(const std::string& response, std::vector<ScraperSearchResult>& results)
{
	Document doc;
	doc.Parse(response.c_str());

	if (doc.HasParseError())
	{
		LOG(LogWarning) << "ProtonDBRequest - Error parsing JSON: " << GetParseError_En(doc.GetParseError());
		return true;
	}

	auto tier = getStringMember(doc, "tier");
	auto confidence = getStringMember(doc, "confidence");
	auto trendingTier = getStringMember(doc, "trendingTier");

	if (tier.empty() && confidence.empty() && trendingTier.empty())
		return true;

	ScraperSearchResult result("ProtonDB");
	if (mGame != nullptr)
		result.mdl = mGame->getMetadata();

	if (mGame != nullptr)
		result.mdl.set(MetaDataId::Name, mGame->getName());

	result.mdl.set(MetaDataId::SteamAppId, mAppId);
	result.mdl.set(MetaDataId::ProtonDBUrl, PROTONDB_APP_URL + mAppId);

	if (!tier.empty())
		result.mdl.set(MetaDataId::ProtonDBTier, tier);
	if (!confidence.empty())
		result.mdl.set(MetaDataId::ProtonDBConfidence, confidence);
	if (!trendingTier.empty())
		result.mdl.set(MetaDataId::ProtonDBTrendingTier, trendingTier);
	if (doc.HasMember("score") && doc["score"].IsNumber())
		result.mdl.set(MetaDataId::ProtonDBScore, std::to_string(doc["score"].GetDouble()));
	if (doc.HasMember("total") && doc["total"].IsInt())
		result.mdl.set(MetaDataId::ProtonDBTotal, std::to_string(doc["total"].GetInt()));

	results.push_back(result);
	return true;
}
