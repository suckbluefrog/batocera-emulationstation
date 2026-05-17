#include "scrapers/LocalLauncherMetadataScraper.h"

#include "FileData.h"
#include "Paths.h"
#include "SystemData.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <rapidjson/document.h>

using namespace rapidjson;

namespace
{
	struct LauncherEntry
	{
		std::string source;
		std::string store;
		std::string id;
		std::string title;
		std::string description;
		std::string developer;
		std::string publisher;
		std::string installPath;
		std::string executable;
		std::string runner;
		std::string winePrefix;
		std::string wineVersion;
		std::string storeUrl;
		std::string cloudSave;
		std::string epicNamespace;
		std::string gogId;
		std::vector<std::string> aliases;
	};

	std::string normalizeName(const std::string& value)
	{
		auto lower = Utils::String::toLower(value);
		std::string normalized;
		for (auto c : lower)
			if (std::isalnum(static_cast<unsigned char>(c)))
				normalized += c;

		return normalized;
	}

	std::string getStringMember(const Value& value, const char* key)
	{
		if (!value.IsObject() || !value.HasMember(key))
			return "";

		const Value& member = value[key];
		if (member.IsString())
			return member.GetString();
		if (member.IsInt64())
			return std::to_string(member.GetInt64());
		if (member.IsUint64())
			return std::to_string(member.GetUint64());
		if (member.IsBool())
			return member.GetBool() ? "true" : "false";

		return "";
	}

	std::string getNestedString(const Value& value, const char* key1, const char* key2)
	{
		if (!value.IsObject() || !value.HasMember(key1) || !value[key1].IsObject())
			return "";

		return getStringMember(value[key1], key2);
	}

	bool readJson(const std::string& path, Document& doc)
	{
		if (!Utils::FileSystem::isRegularFile(path))
			return false;

		auto content = Utils::FileSystem::readAllText(path);
		if (content.empty())
			return false;

		doc.Parse(content.c_str());
		return !doc.HasParseError();
	}

	void addAlias(LauncherEntry& entry, const std::string& alias)
	{
		auto trimmed = Utils::String::trim(alias);
		if (!trimmed.empty())
			entry.aliases.push_back(trimmed);
	}

	void applyHeroicGameConfig(const std::string& base, LauncherEntry& entry)
	{
		if (entry.id.empty())
			return;

		Document doc;
		auto path = Utils::FileSystem::combine(Utils::FileSystem::combine(base, "GamesConfig"), entry.id + ".json");
		if (!readJson(path, doc) || !doc.IsObject())
			return;

		const Value* config = nullptr;
		if (doc.HasMember(entry.id.c_str()) && doc[entry.id.c_str()].IsObject())
			config = &doc[entry.id.c_str()];
		else
		{
			for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it)
			{
				if (it->value.IsObject())
				{
					config = &it->value;
					break;
				}
			}
		}

		if (config == nullptr)
			return;

		if (entry.winePrefix.empty())
			entry.winePrefix = getStringMember(*config, "winePrefix");
		if (entry.wineVersion.empty() && config->HasMember("wineVersion") && (*config)["wineVersion"].IsObject())
			entry.wineVersion = getStringMember((*config)["wineVersion"], "name");
	}

	void addHeroicLibraryEntry(std::vector<LauncherEntry>& entries, const std::string& base, const Value& value, const std::string& fallbackStore)
	{
		if (!value.IsObject())
			return;

		LauncherEntry entry;
		entry.source = "Heroic";
		entry.runner = getStringMember(value, "runner");
		entry.store = fallbackStore;
		if (entry.store.empty())
			entry.store = entry.runner == "gog" ? "GOG" : "Epic";

		entry.id = getStringMember(value, "app_name");
		entry.title = getStringMember(value, "title");
		entry.developer = getStringMember(value, "developer");
		entry.publisher = getStringMember(value, "publisher");
		entry.description = getNestedString(value, "extra", "description");
		if (entry.description.empty() && value.HasMember("extra") && value["extra"].IsObject())
			entry.description = getNestedString(value["extra"], "about", "description");

		entry.storeUrl = getStringMember(value, "store_url");
		if (entry.storeUrl.empty() && value.HasMember("extra") && value["extra"].IsObject())
			entry.storeUrl = getStringMember(value["extra"], "storeUrl");

		if (value.HasMember("install") && value["install"].IsObject())
		{
			entry.installPath = getStringMember(value["install"], "install_path");
			entry.executable = getStringMember(value["install"], "executable");
		}

		entry.cloudSave = getStringMember(value, "save_folder");
		if (entry.cloudSave.empty())
			entry.cloudSave = getStringMember(value, "save_path");
		entry.epicNamespace = getStringMember(value, "namespace");
		if (entry.store == "GOG")
			entry.gogId = entry.id;

		addAlias(entry, entry.title);
		addAlias(entry, entry.id);
		addAlias(entry, getStringMember(value, "folder_name"));

		applyHeroicGameConfig(base, entry);
		if (!entry.title.empty() || !entry.id.empty())
			entries.push_back(entry);
	}

	void addHeroicInstalledEntry(std::vector<LauncherEntry>& entries, const std::string& base, const std::string& key, const Value& value)
	{
		if (!value.IsObject())
			return;

		LauncherEntry entry;
		entry.source = "Heroic";
		entry.store = "Epic";
		entry.runner = "legendary";
		entry.id = key;
		entry.title = getStringMember(value, "title");
		entry.installPath = getStringMember(value, "install_path");
		entry.executable = getStringMember(value, "executable");
		entry.cloudSave = getStringMember(value, "save_path");
		addAlias(entry, entry.title);
		addAlias(entry, entry.id);
		applyHeroicGameConfig(base, entry);
		if (!entry.title.empty() || !entry.id.empty())
			entries.push_back(entry);
	}

	void loadHeroicBase(std::vector<LauncherEntry>& entries, const std::string& base)
	{
		if (!Utils::FileSystem::isDirectory(base))
			return;

		for (auto item : { std::make_pair("store_cache/legendary_library.json", "Epic"), std::make_pair("store_cache/gog_library.json", "GOG") })
		{
			Document doc;
			auto path = Utils::FileSystem::combine(base, item.first);
			if (!readJson(path, doc) || !doc.IsObject() || !doc.HasMember("library") || !doc["library"].IsArray())
				continue;

			const Value& library = doc["library"];
			for (SizeType i = 0; i < library.Size(); i++)
				addHeroicLibraryEntry(entries, base, library[i], item.second);
		}

		Document installed;
		auto installedPath = Utils::FileSystem::combine(base, "legendaryConfig/legendary/installed.json");
		if (readJson(installedPath, installed) && installed.IsObject())
		{
			for (auto it = installed.MemberBegin(); it != installed.MemberEnd(); ++it)
				addHeroicInstalledEntry(entries, base, it->name.GetString(), it->value);
		}
	}

	std::map<std::string, std::string> readSimpleYaml(const std::string& path)
	{
		std::map<std::string, std::string> values;
		for (auto line : Utils::FileSystem::readAllLines(path))
		{
			line = Utils::String::trim(line);
			if (line.empty() || line[0] == '#')
				continue;

			auto pos = line.find(':');
			if (pos == std::string::npos)
				continue;

			auto key = Utils::String::trim(line.substr(0, pos));
			auto value = Utils::String::trim(line.substr(pos + 1));
			if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'')))
				value = value.substr(1, value.size() - 2);

			if (!key.empty() && !value.empty())
				values[key] = value;
		}

		return values;
	}

	void loadLutrisDir(std::vector<LauncherEntry>& entries, const std::string& dir)
	{
		if (!Utils::FileSystem::isDirectory(dir))
			return;

		for (auto path : Utils::FileSystem::getDirContent(dir, true))
		{
			auto extension = Utils::String::toLower(Utils::FileSystem::getExtension(path));
			if (extension != ".yml" && extension != ".yaml")
				continue;

			auto data = readSimpleYaml(path);
			LauncherEntry entry;
			entry.source = "Lutris";
			entry.store = "Lutris";
			entry.title = data["name"];
			entry.id = data["slug"].empty() ? data["game_slug"] : data["slug"];
			entry.runner = data["runner"];
			entry.installPath = data["directory"].empty() ? data["path"] : data["directory"];
			addAlias(entry, entry.title);
			addAlias(entry, entry.id);

			if (!entry.title.empty() || !entry.id.empty())
				entries.push_back(entry);
		}
	}

	std::vector<LauncherEntry> loadEntries()
	{
		std::vector<LauncherEntry> entries;
		auto home = Paths::getHomePath();

		loadHeroicBase(entries, Utils::FileSystem::combine(home, ".config/heroic"));
		loadHeroicBase(entries, Utils::FileSystem::combine(home, ".var/app/com.heroicgameslauncher.hgl/config/heroic"));
		loadLutrisDir(entries, Utils::FileSystem::combine(home, ".local/share/lutris/games"));
		loadLutrisDir(entries, Utils::FileSystem::combine(home, ".config/lutris/games"));

		return entries;
	}

	bool matchesEntry(FileData* game, const std::string& searchName, const LauncherEntry& entry)
	{
		auto normalizedSearch = normalizeName(searchName);
		auto normalizedPath = normalizeName(Utils::FileSystem::getStem(game->getPath()));
		auto path = game->getPath();

		std::vector<std::string> candidates = entry.aliases;
		if (!entry.title.empty())
			candidates.push_back(entry.title);
		if (!entry.id.empty())
			candidates.push_back(entry.id);

		for (auto alias : candidates)
		{
			auto normalizedAlias = normalizeName(alias);
			if (normalizedAlias.empty())
				continue;

			if (normalizedAlias == normalizedSearch || normalizedAlias == normalizedPath)
				return true;
		}

		if (!entry.installPath.empty() && path.find(entry.installPath) != std::string::npos)
			return true;
		if (!entry.executable.empty() && path.find(entry.executable) != std::string::npos)
			return true;

		auto launcherId = game->getMetadata(MetaDataId::LauncherId);
		if (!launcherId.empty() && launcherId == entry.id)
			return true;

		auto epicNamespace = game->getMetadata(MetaDataId::EpicNamespace);
		if (!epicNamespace.empty() && epicNamespace == entry.epicNamespace)
			return true;

		auto gogId = game->getMetadata(MetaDataId::GogId);
		if (!gogId.empty() && gogId == entry.gogId)
			return true;

		return false;
	}

	void addResult(std::vector<ScraperSearchResult>& results, FileData* game, const LauncherEntry& entry)
	{
		ScraperSearchResult result("LocalLaunchers");
		if (game != nullptr)
			result.mdl = game->getMetadata();

		if (!entry.title.empty())
			result.mdl.set(MetaDataId::Name, entry.title);
		if (!entry.description.empty())
			result.mdl.set(MetaDataId::Desc, entry.description);
		if (!entry.developer.empty())
			result.mdl.set(MetaDataId::Developer, entry.developer);
		if (!entry.publisher.empty())
			result.mdl.set(MetaDataId::Publisher, entry.publisher);

		result.mdl.set(MetaDataId::LauncherSource, entry.source);
		result.mdl.set(MetaDataId::LauncherStore, entry.store);
		result.mdl.set(MetaDataId::LauncherId, entry.id);
		result.mdl.set(MetaDataId::LauncherInstallPath, entry.installPath);
		result.mdl.set(MetaDataId::LauncherExecutable, entry.executable);
		result.mdl.set(MetaDataId::LauncherRunner, entry.runner);
		result.mdl.set(MetaDataId::LauncherWinePrefix, entry.winePrefix);
		result.mdl.set(MetaDataId::LauncherWineVersion, entry.wineVersion);
		result.mdl.set(MetaDataId::LauncherStoreUrl, entry.storeUrl);
		result.mdl.set(MetaDataId::LauncherCloudSave, entry.cloudSave);
		result.mdl.set(MetaDataId::EpicNamespace, entry.epicNamespace);
		result.mdl.set(MetaDataId::GogId, entry.gogId);

		results.push_back(result);
	}
}

void LocalLauncherMetadataScraper::generateRequests(const ScraperSearchParams& params,
	std::queue<std::unique_ptr<ScraperRequest>>& requests,
	std::vector<ScraperSearchResult>& results)
{
	auto cleanName = params.nameOverride;
	if (cleanName.empty())
		cleanName = Utils::String::removeParenthesis(Utils::FileSystem::getStem(params.game->getPath()));

	requests.push(std::unique_ptr<ScraperRequest>(new LocalLauncherMetadataRequest(results, params.game, Utils::String::trim(cleanName))));
}

bool LocalLauncherMetadataScraper::isSupportedPlatform(SystemData* system)
{
	return system != nullptr;
}

const std::set<Scraper::ScraperMediaSource>& LocalLauncherMetadataScraper::getSupportedMedias()
{
	static std::set<ScraperMediaSource> mdds = {};
	return mdds;
}

LocalLauncherMetadataRequest::LocalLauncherMetadataRequest(std::vector<ScraperSearchResult>& resultsWrite, FileData* game, const std::string& searchName)
	: ScraperRequest(resultsWrite)
{
	static const std::vector<LauncherEntry> entries = loadEntries();

	for (auto entry : entries)
		if (matchesEntry(game, searchName, entry))
			addResult(mResults, game, entry);

	setStatus(ASYNC_DONE);
}

void LocalLauncherMetadataRequest::update()
{
}
