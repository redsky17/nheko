#pragma once

#include <QImage>
#include <QQuickAsyncImageProvider>
#include <QQuickImageResponse>
#include <QThreadPool>

#include <mtx/common.hpp>

#include <boost/optional.hpp>

#include "jdenticoninterface.h"

namespace Jdenticon {
JdenticonInterface *
getJdenticonInterface();
}

class JdenticonResponse
  : public QQuickImageResponse
  , public QRunnable
{
public:
        JdenticonResponse(const QString &key, const QSize &requestedSize);

        QQuickTextureFactory *textureFactory() const override
        {
                return QQuickTextureFactory::textureFactoryForImage(m_pixmap.toImage());
        }

        void run() override;

        QString m_key;
        QSize m_requestedSize;
        QPixmap m_pixmap;
        JdenticonInterface *jdenticonInterface_ = nullptr;
};

class JdenticonProvider
  : public QObject
  , public QQuickAsyncImageProvider
{
        Q_OBJECT

public:
        static bool isAvailable() { return Jdenticon::getJdenticonInterface() != nullptr; }

public slots:
        QQuickImageResponse *requestImageResponse(const QString &key,
                                                  const QSize &requestedSize) override
        {
                JdenticonResponse *response = new JdenticonResponse(key, requestedSize);
                pool.start(response);
                return response;
        }

private:
        QThreadPool pool;
};
