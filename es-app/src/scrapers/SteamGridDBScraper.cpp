#include "scrapers/SteamGridDBScraper.h"

#include "FileData.h"
#include "Log.h"
#include "Paths.h"
#include "Settings.h"
#include "SystemConf.h"
#include "SystemData.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <stdexcept>
#include <vector>

using namespace rapidjson;

namespace
{
	const std::string STEAMGRIDDB_API_URL_BASE = "https://www.steamgriddb.com/api/v2";
	const std::string STEAMGRIDDB_API_KEY_FILE_NAME = "steamgriddb.key";
	const int STEAMGRIDDB_AUTO_ASSET_LIMIT = 1;
	const int STEAMGRIDDB_MANUAL_ASSET_LIMIT = 6;

	std::vector<std::string> getSteamGridDBApiKeyFilePaths()
	{
		std::vector<std::string> paths;
		paths.push_back(Utils::FileSystem::combine(Paths::getUserEmulationStationPath(), STEAMGRIDDB_API_KEY_FILE_NAME));
		paths.push_back("/userdata/system/" + STEAMGRIDDB_API_KEY_FILE_NAME);
		return paths;
	}

	std::string unwrapSteamGridDBApiKey(std::string key)
	{
		key = Utils::String::trim(key);
		if (key.size() >= 2 && ((key.front() == '"' && key.back() == '"') || (key.front() == '\'' && key.back() == '\'')))
			key = Utils::String::trim(key.substr(1, key.size() - 2));

		return key;
	}

	std::string extractSteamGridDBApiKey(const std::string& contents)
	{
		for (auto line : Utils::String::splitAny(contents, "\r\n", true))
		{
			line = Utils::String::trim(line);
			if (line.empty() || Utils::String::startsWith(line, "#") || Utils::String::startsWith(line, ";"))
				continue;

			auto equalPos = line.find('=');
			if (equalPos != std::string::npos)
			{
				auto key = Utils::String::trim(line.substr(0, equalPos));
				if (key == "steamgriddb.api_key" || key == "api_key" || key == "key")
					line = line.substr(equalPos + 1);
				else
					continue;
			}

			line = unwrapSteamGridDBApiKey(line);
			if (!line.empty())
				return line;
		}

		return "";
	}

	std::string getSteamGridDBApiKey()
	{
		auto apiKey = unwrapSteamGridDBApiKey(SystemConf::getInstance()->get("steamgriddb.api_key"));
		if (!apiKey.empty())
			return apiKey;

		for (auto path : getSteamGridDBApiKeyFilePaths())
		{
			if (!Utils::FileSystem::exists(path))
				continue;

			apiKey = extractSteamGridDBApiKey(Utils::FileSystem::readAllText(path));
			if (!apiKey.empty())
				return apiKey;
		}

		return "";
	}

	HttpReqOptions getSteamGridDBOptions()
	{
		HttpReqOptions options;
		options.customHeaders.push_back("Accept: application/json");
		options.customHeaders.push_back("Authorization: Bearer " + getSteamGridDBApiKey());
		return options;
	}

	std::string getSteamAppId(FileData* game)
	{
		if (game == nullptr || Utils::FileSystem::getExtension(game->getPath()) != ".steam")
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

			auto appid = Utils::String::trim(line.substr(6));
			if (!appid.empty() && std::all_of(appid.cbegin(), appid.cend(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
				return appid;
		}

		return "";
	}

	std::string getCleanGameName(const ScraperSearchParams& params)
	{
		auto cleanName = params.nameOverride;
		if (cleanName.empty())
			cleanName = Utils::String::removeParenthesis(Utils::FileSystem::getStem(params.game->getPath()));

		return cleanName;
	}

	std::string getAssetLimitQuery(bool manualScrape)
	{
		return std::to_string(manualScrape ? STEAMGRIDDB_MANUAL_ASSET_LIMIT : STEAMGRIDDB_AUTO_ASSET_LIMIT);
	}

	std::string addGridFilters(const std::string& url, bool manualScrape)
	{
		return url + "?dimensions=600x900,342x482,660x930"
			+ "&types=static"
			+ "&mimes=image%2Fpng,image%2Fjpeg"
			+ "&nsfw=false"
			+ "&humor=false"
			+ "&epilepsy=false"
			+ "&limit=" + getAssetLimitQuery(manualScrape);
	}

	std::string addHeroFilters(const std::string& url, bool manualScrape)
	{
		return url + "?types=static"
			+ "&mimes=image%2Fpng,image%2Fjpeg"
			+ "&nsfw=false"
			+ "&humor=false"
			+ "&epilepsy=false"
			+ "&limit=" + getAssetLimitQuery(manualScrape);
	}

	std::string addLogoFilters(const std::string& url, bool manualScrape)
	{
		return url + "?types=static"
			+ "&mimes=image%2Fpng"
			+ "&nsfw=false"
			+ "&humor=false"
			+ "&epilepsy=false"
			+ "&limit=" + getAssetLimitQuery(manualScrape);
	}

	std::string selectAsset(const std::vector<std::string>& assets, size_t index)
	{
		if (assets.empty())
			return "";

		if (index < assets.size())
			return assets[index];

		return assets.front();
	}
}

void SteamGridDBScraper::generateRequests(const ScraperSearchParams& params,
	std::queue<std::unique_ptr<ScraperRequest>>& requests,
	std::vector<ScraperSearchResult>& results)
{
	if (getSteamGridDBApiKey().empty())
	{
		if (!params.isManualScrape)
			throw std::runtime_error("INVALID CREDENTIALS");

		return;
	}

	auto options = getSteamGridDBOptions();
	auto appid = getSteamAppId(params.game);

	if (!appid.empty())
	{
		auto path = STEAMGRIDDB_API_URL_BASE + "/games/steam/" + HttpReq::urlEncode(appid);
		requests.push(std::unique_ptr<ScraperRequest>(new SteamGridDBRequest(results, path, &options, SteamGridDBRequest::SteamAppId, params.isManualScrape)));
		return;
	}

	auto cleanName = getCleanGameName(params);
	if (cleanName.empty())
		return;

	auto path = STEAMGRIDDB_API_URL_BASE + "/search/autocomplete/" + HttpReq::urlEncode(cleanName);
	requests.push(std::unique_ptr<ScraperRequest>(new SteamGridDBRequest(results, path, &options, SteamGridDBRequest::SearchByName, params.isManualScrape)));
}

bool SteamGridDBScraper::isSupportedPlatform(SystemData* system)
{
	return system != nullptr;
}

const std::set<Scraper::ScraperMediaSource>& SteamGridDBScraper::getSupportedMedias()
{
	static std::set<ScraperMediaSource> mdds =
	{
		ScraperMediaSource::Box2d,
		ScraperMediaSource::FanArt,
		ScraperMediaSource::Marquee,
		ScraperMediaSource::Wheel
	};

	return mdds;
}

std::vector<SteamGridDBRequest::Game> SteamGridDBRequest::parseGames(const std::string& response)
{
	std::vector<Game> games;
	Document doc;
	doc.Parse(response.c_str());

	if (doc.HasParseError())
	{
		LOG(LogWarning) << "SteamGridDBRequest - Error parsing JSON: " << GetParseError_En(doc.GetParseError());
		return games;
	}

	if (!doc.HasMember("success") || !doc["success"].IsBool() || !doc["success"].GetBool() || !doc.HasMember("data"))
		return games;

	auto addGame = [&games](const Value& game)
	{
		if (!game.IsObject() || !game.HasMember("id") || !game["id"].IsInt() || !game.HasMember("name") || !game["name"].IsString())
			return;

		Game item;
		item.id = game["id"].GetInt();
		item.name = game["name"].GetString();
		games.push_back(item);
	};

	const Value& data = doc["data"];
	if (mLookupMode == SteamAppId && data.IsObject())
		addGame(data);
	else if (data.IsArray())
	{
		for (SizeType i = 0; i < data.Size() && i < 8; i++)
			addGame(data[i]);
	}

	return games;
}

void SteamGridDBRequest::preProcess(const std::string& response)
{
	mGames = parseGames(response);

	for (auto game : mGames)
	{
		auto id = std::to_string(game.id);
		mDependencyQueue.push({ "grid:" + id, addGridFilters(STEAMGRIDDB_API_URL_BASE + "/grids/game/" + id, mIsManualScrape) });
		mDependencyQueue.push({ "hero:" + id, addHeroFilters(STEAMGRIDDB_API_URL_BASE + "/heroes/game/" + id, mIsManualScrape) });
		mDependencyQueue.push({ "logo:" + id, addLogoFilters(STEAMGRIDDB_API_URL_BASE + "/logos/game/" + id, mIsManualScrape) });
	}
}

std::vector<std::string> SteamGridDBRequest::getAssetUrls(const std::string& dependencyId)
{
	std::vector<std::string> urls;
	auto response = getDependencyResponse(dependencyId);
	if (response.empty())
		return urls;

	Document doc;
	doc.Parse(response.c_str());

	if (doc.HasParseError() || !doc.HasMember("success") || !doc["success"].IsBool() || !doc["success"].GetBool() || !doc.HasMember("data") || !doc["data"].IsArray())
		return urls;

	const Value& data = doc["data"];
	for (SizeType i = 0; i < data.Size(); i++)
	{
		if (!data[i].IsObject() || !data[i].HasMember("url") || !data[i]["url"].IsString())
			continue;

		urls.push_back(data[i]["url"].GetString());
	}

	return urls;
}

bool SteamGridDBRequest::process(const std::string& response, std::vector<ScraperSearchResult>& results)
{
	if (mGames.empty())
		mGames = parseGames(response);

	for (auto game : mGames)
	{
		auto id = std::to_string(game.id);
		auto grids = getAssetUrls("grid:" + id);
		auto heroes = getAssetUrls("hero:" + id);
		auto logos = getAssetUrls("logo:" + id);

		size_t variantCount = mIsManualScrape ? std::max({ grids.size(), heroes.size(), logos.size(), static_cast<size_t>(1) }) : 1;
		for (size_t i = 0; i < variantCount; i++)
		{
			auto grid = selectAsset(grids, i);
			auto hero = selectAsset(heroes, i);
			auto logo = selectAsset(logos, i);

			if (grid.empty() && hero.empty() && logo.empty())
				continue;

			ScraperSearchResult result("SteamGridDB");
			result.mdl.set(MetaDataId::Name, game.name);
			if (mIsManualScrape && variantCount > 1)
				result.displayName = game.name + " [SteamGridDB art " + std::to_string(i + 1) + "/" + std::to_string(variantCount) + "]";

			auto imageSource = Settings::getInstance()->getString("ScrapperImageSrc");
			if (!imageSource.empty())
			{
				if (imageSource == "fanart" && !hero.empty())
					result.urls[MetaDataId::Image] = ScraperSearchItem(hero);
				else if (!grid.empty())
					result.urls[MetaDataId::Image] = ScraperSearchItem(grid);
			}

			if (!Settings::getInstance()->getString("ScrapperThumbSrc").empty() && !grid.empty())
				result.urls[MetaDataId::Thumbnail] = ScraperSearchItem(grid);

			if (Settings::getInstance()->getBool("ScrapeFanart") && !hero.empty())
				result.urls[MetaDataId::FanArt] = ScraperSearchItem(hero);

			auto logoSource = Settings::getInstance()->getString("ScrapperLogoSrc");
			if (!logoSource.empty())
			{
				if (logoSource == "marquee" && !hero.empty())
					result.urls[MetaDataId::Marquee] = ScraperSearchItem(hero);
				else if (!logo.empty())
					result.urls[MetaDataId::Marquee] = ScraperSearchItem(logo);
				else if (!hero.empty())
					result.urls[MetaDataId::Marquee] = ScraperSearchItem(hero);
			}

			if (result.hasMedia())
				results.push_back(result);
		}
	}

	return true;
}
