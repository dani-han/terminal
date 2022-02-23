// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <dwrite_3.h>

namespace Microsoft::Console::Render::FontCache
{
    namespace details
    {
        struct Cache
        {
            void Invalidate() noexcept
            {
                _lock.lock();
                _fontCollection.reset();
                _lock.unlock();
            }

            wil::com_ptr<IDWriteFontCollection> Get(bool forceUpdate)
            {
                std::lock_guard guard{ _lock };

                if (!_fontCollection || forceUpdate)
                {
                    _init(forceUpdate);
                    FAIL_FAST_IF(!_fontCollection);
                }

                return _fontCollection;
            }

        private:
            void _init(bool forceUpdate)
            {
                // DWRITE_FACTORY_TYPE_SHARED _should_ return the same instance every time.
                wil::com_ptr<IDWriteFactory> factory;
                THROW_IF_FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(factory), reinterpret_cast<::IUnknown**>(factory.addressof())));

                wil::com_ptr<IDWriteFontCollection> systemFontCollection;
                THROW_IF_FAILED(factory->GetSystemFontCollection(systemFontCollection.addressof(), forceUpdate));

                // IDWriteFactory5 is supported since Windows 10, build 15021.
                const auto factory5 = factory.try_query<IDWriteFactory5>();
                if (!factory5)
                {
                    _fontCollection = systemFontCollection;
                    return;
                }

                // Cache nearby files.
                if (!_nearbyFilesFound)
                {
                    _nearbyFilesFound = true;

                    const std::filesystem::path module{ wil::GetModuleFileNameW<std::wstring>(nullptr) };
                    const auto folder{ module.parent_path() };

                    for (const auto& p : std::filesystem::directory_iterator(folder))
                    {
                        if (til::ends_with(p.path().native(), L".ttf"))
                        {
                            wil::com_ptr<IDWriteFontFile> fontFile;
                            if (SUCCEEDED_LOG(factory5->CreateFontFileReference(p.path().c_str(), nullptr, fontFile.addressof())))
                            {
                                _nearbyFiles.emplace_back(std::move(fontFile));
                            }
                        }
                    }

                    _nearbyFiles.shrink_to_fit();
                }

                wil::com_ptr<IDWriteFontSet> systemFontSet;
                // IDWriteFontCollection1 is supported since Windows 7.
                THROW_IF_FAILED(systemFontCollection.query<IDWriteFontCollection1>()->GetFontSet(systemFontSet.addressof()));

                wil::com_ptr<IDWriteFontSetBuilder1> fontSetBuilder;
                THROW_IF_FAILED(factory5->CreateFontSetBuilder(fontSetBuilder.addressof()));
                THROW_IF_FAILED(fontSetBuilder->AddFontSet(systemFontSet.get()));

                for (const auto& file : _nearbyFiles)
                {
                    LOG_IF_FAILED(fontSetBuilder->AddFontFile(file.get()));
                }

                wil::com_ptr<IDWriteFontSet> fontSet;
                THROW_IF_FAILED(fontSetBuilder->CreateFontSet(fontSet.addressof()));

                wil::com_ptr<IDWriteFontCollection1> fontCollection;
                THROW_IF_FAILED(factory5->CreateFontCollectionFromFontSet(fontSet.get(), fontCollection.addressof()));

                _fontCollection = std::move(fontCollection);
            }

            std::shared_mutex _lock;
            wil::com_ptr<IDWriteFontCollection> _fontCollection;
            std::vector<wil::com_ptr<IDWriteFontFile>> _nearbyFiles;
            bool _nearbyFilesFound = false;
        };

        inline static Cache cache;
    }

    inline void Invalidate() noexcept
    {
        details::cache.Invalidate();
    }

    inline wil::com_ptr<IDWriteFontCollection> GetCached()
    {
        return details::cache.Get(false);
    }

    inline wil::com_ptr<IDWriteFontCollection> GetFresh()
    {
        return details::cache.Get(true);
    }
}
