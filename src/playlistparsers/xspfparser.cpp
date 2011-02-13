/* This file is part of Clementine.
   Copyright 2010, David Sansome <me@davidsansome.com>

   Clementine is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Clementine is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Clementine.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "xspfparser.h"

#include <QDomDocument>
#include <QFile>
#include <QIODevice>
#include <QRegExp>
#include <QUrl>
#include <QXmlStreamReader>

XSPFParser::XSPFParser(LibraryBackendInterface* library, QObject* parent)
    : XMLParser(library, parent)
{
}

SongList XSPFParser::Load(QIODevice *device, const QString& playlist_path, const QDir&) const {
  SongList ret;

  QXmlStreamReader reader(device);
  if (!ParseUntilElement(&reader, "playlist") ||
      !ParseUntilElement(&reader, "trackList")) {
    return ret;
  }

  while (!reader.atEnd() && ParseUntilElement(&reader, "track")) {
    Song song = ParseTrack(&reader);
    if (song.is_valid()) {
      ret << song;
    }
  }
  return ret;
}

Song XSPFParser::ParseTrack(QXmlStreamReader* reader) const {
  Song song;
  QString title, artist, album;
  int length = -1;
  while (!reader->atEnd()) {
    QXmlStreamReader::TokenType type = reader->readNext();
    switch (type) {
      case QXmlStreamReader::StartElement: {
        QStringRef name = reader->name();
        if (name == "location") {
          QUrl url(reader->readElementText());
          if (url.scheme() == "file") {
            QString filename = url.toLocalFile();
            if (!QFile::exists(filename)) {
              return Song();
            }

            // Load the song from the library if it's there.
            Song library_song = LoadLibrarySong(filename);
            if (library_song.is_valid())
              return library_song;

            song.InitFromFile(filename, -1);
            return song;
          } else {
            song.set_filename(url.toString());
            song.set_filetype(Song::Type_Stream);
          }
        } else if (name == "title") {
          title = reader->readElementText();
        } else if (name == "creator") {
          artist = reader->readElementText();
        } else if (name == "album") {
          album = reader->readElementText();
        } else if (name == "duration") {  // in milliseconds.
          const QString& duration = reader->readElementText();
          bool ok = false;
          length = duration.toInt(&ok) / 1000;
          if (!ok) {
            length = -1;
          }
        } else if (name == "image") {
          // TODO: Fetch album covers.
        } else if (name == "info") {
          // TODO: Do something with extra info?
        }
        break;
      }
      case QXmlStreamReader::EndElement: {
        if (reader->name() == "track") {
          song.Init(title, artist, album, length);
          return song;
        }
      }
      default:
        break;
    }
  }
  // At least make an effort if we never find a </track>.
  song.Init(title, artist, album, length);
  return song;
}

void XSPFParser::Save(const SongList& songs, QIODevice* device, const QDir&) const {
  QXmlStreamWriter writer(device);
  writer.writeStartDocument();
  StreamElement playlist("playlist", &writer);
  writer.writeAttribute("version", "1");
  writer.writeDefaultNamespace("http://xspf.org/ns/0/");

  StreamElement tracklist("trackList", &writer);
  foreach (const Song& song, songs) {
    StreamElement track("track", &writer);
    writer.writeTextElement("location", MakeUrl(song.filename()));
    writer.writeTextElement("title", song.title());
    if (!song.artist().isEmpty()) {
      writer.writeTextElement("creator", song.artist());
    }
    if (!song.album().isEmpty()) {
      writer.writeTextElement("album", song.album());
    }
    if (song.length_nanosec() != -1) {
      writer.writeTextElement("duration", QString::number(song.length_nanosec() / 1e6));
    }

    QString art = song.art_manual().isEmpty() ? song.art_automatic() : song.art_manual();
    // Ignore images that are in our resource bundle.
    if (!art.startsWith(":") && !art.isEmpty()) {
      // Convert local files to URLs.
      art = MakeUrl(art);
      writer.writeTextElement("image", art);
    }
  }
  writer.writeEndDocument();
}

bool XSPFParser::TryMagic(const QByteArray &data) const {
  return data.contains("<playlist") && data.contains("<trackList");
}
