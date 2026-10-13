#pragma once

#import <Foundation/Foundation.h>
#include <vibranceUI/media/session.h>
#include <deque>
#include <list>
#include <string>
#include <unistd.h>

// Worker-owned, bounded track cache. Missing artwork is an incomplete response,
// not an instruction to replace an already known cover with a placeholder.
struct MacMediaArtworkCache
{
    struct Entry
    {
        std::string key;
        std::string source;
        std::string item;
        std::string path;
        NSData* data = nil;
    };
    std::list<Entry> entries;
    std::deque<std::string> retired;
    std::string visiblePath;
    std::uint64_t revision = 0;
    std::uint64_t fileSerial = 0;

    static std::string identity(const MediaSessionSnapshot& snapshot)
    {
        std::string result;
        const auto append = [&](const std::string& part) {
            result += std::to_string(part.size()) + ":" + part;
        };
        append(snapshot.sourceApp);
        if (snapshot.sourceApp.empty()) append(std::to_string(snapshot.sourceProcessId));
        append(snapshot.title);
        append(snapshot.artist);
        append(snapshot.album);
        return result;
    }

    void retire(std::string path)
    {
        if (!path.empty()) retired.push_back(std::move(path));
        while (retired.size() > 8)
        {
            unlink(retired.front().c_str());
            retired.pop_front();
        }
    }

    void update(MediaSessionSnapshot& snapshot, NSDictionary* payload, NSString* directory)
    {
        std::string path;
        if (snapshot.sessionAvailable)
        {
            const auto key = identity(snapshot);
            const auto source = snapshot.sourceApp.empty() ?
                std::to_string(snapshot.sourceProcessId) : snapshot.sourceApp;
            std::string item;
            for (NSString* field in @[@"contentItemIdentifier", @"uniqueIdentifier"])
            {
                id value = payload[field];
                if ([value isKindOfClass:NSString.class] && [value length])
                { item = [value UTF8String]; break; }
            }
            auto found = entries.begin();
            while (found != entries.end())
            {
                const bool sameItem = !item.empty() && found->item == item;
                // Spotify regenerates contentItemIdentifier on play/pause and
                // temporarily omits artwork. The visible track identity wins
                // over these transport-scoped IDs; fresh bytes still replace it.
                const bool sameMetadata = found->key == key;
                if (found->source == source && (sameItem || sameMetadata)) break;
                ++found;
            }
            if (found == entries.end())
            {
                entries.push_front({key, source, item, {}, nil});
                found = entries.begin();
            }
            else entries.splice(entries.begin(), entries, found);
            Entry& entry = entries.front();
            // Item IDs can arrive later than metadata or disappear on pause.
            if (!item.empty()) entry.item = item;
            entry.key = key;
            id encoded = payload[@"artworkData"];
            NSData* data = [encoded isKindOfClass:NSString.class] ?
                [[NSData alloc] initWithBase64EncodedString:encoded options:0] : nil;
            if (data.length && ![data isEqualToData:entry.data] && directory)
            {
                NSString* file = [directory stringByAppendingPathComponent:
                    [NSString stringWithFormat:@"%llu.img", static_cast<unsigned long long>(++fileSerial)]];
                // Commit only after a successful write; a failure can be retried.
                if ([data writeToFile:file options:NSDataWritingAtomic error:nil])
                {
                    retire(std::move(entry.path));
                    entry.path = file.UTF8String;
                    entry.data = data;
                }
            }
            path = entry.path;
            while (entries.size() > 8)
            {
                retire(std::move(entries.back().path));
                entries.pop_back();
            }
        }
        if (path != visiblePath)
        {
            visiblePath = path;
            ++revision;
        }
        snapshot.thumbnailPath = path;
        snapshot.thumbnailRevision = revision;
    }
};
