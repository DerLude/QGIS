/***************************************************************************
                         qgspointcloudrenderer.cpp
                         --------------------
    begin                : October 2020
    copyright            : (C) 2020 by Peter Petrik
    email                : zilolv at gmail dot com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <QElapsedTimer>

#include "qgspointcloudrenderer.h"
#include "qgspointcloudlayer.h"
#include "qgsrendercontext.h"
#include "qgspointcloudindex.h"
#include "qgsstyle.h"
#include "qgscolorramp.h"

///@cond PRIVATE
QgsPointCloudRendererConfig::QgsPointCloudRendererConfig() = default;

QgsPointCloudRendererConfig::QgsPointCloudRendererConfig( const QgsPointCloudRendererConfig &other )
{
  mZMin = other.zMin();
  mZMax = other.zMax();
  mPenWidth = other.penWidth();
  mColorRamp.reset( other.colorRamp()->clone() );
}

QgsPointCloudRendererConfig &QgsPointCloudRendererConfig::QgsPointCloudRendererConfig::operator =( const QgsPointCloudRendererConfig &other )
{
  mZMin = other.zMin();
  mZMax = other.zMax();
  mPenWidth = other.penWidth();
  mColorRamp.reset( other.colorRamp()->clone() );
  return *this;
}

double QgsPointCloudRendererConfig::zMin() const
{
  return mZMin;
}

void QgsPointCloudRendererConfig::setZMin( double value )
{
  mZMin = value;
}

double QgsPointCloudRendererConfig::zMax() const
{
  return mZMax;
}

void QgsPointCloudRendererConfig::setZMax( double value )
{
  mZMax = value;
}

int QgsPointCloudRendererConfig::penWidth() const
{
  return mPenWidth;
}

void QgsPointCloudRendererConfig::setPenWidth( int value )
{
  mPenWidth = value;
}

QgsColorRamp *QgsPointCloudRendererConfig::colorRamp() const
{
  return mColorRamp.get();
}

void QgsPointCloudRendererConfig::setColorRamp( const QgsColorRamp *value )
{
  // TODO should it clone?
  mColorRamp.reset( value->clone() );
}

///@endcond

QgsPointCloudLayerRenderer::QgsPointCloudLayerRenderer( QgsPointCloudLayer *layer, QgsRenderContext &context )
  : QgsMapLayerRenderer( layer->id(), &context )
  , mLayer( layer )
{
  // TODO: we must not keep pointer to mLayer (it's dangerous) - we must copy anything we need for rendering
  // or use some locking to prevent read/write from multiple threads

  // TODO: use config from layer
  mConfig.setPenWidth( context.convertToPainterUnits( 1, QgsUnitTypes::RenderUnit::RenderMillimeters ) );
  // good range for 26850_12580.laz
  mConfig.setZMin( layer->customProperty( QStringLiteral( "pcMin" ), 400 ).toInt() );
  mConfig.setZMax( layer->customProperty( QStringLiteral( "pcMax" ), 600 ).toInt() );
  mConfig.setColorRamp( QgsStyle::defaultStyle()->colorRamp( layer->customProperty( QStringLiteral( "pcRamp" ), QStringLiteral( "Viridis" ) ).toString() ) );
}

static QList<IndexedPointCloudNode> _traverseTree( QgsPointCloudIndex *pc, const QgsRectangle &extent, IndexedPointCloudNode n, int maxDepth )
{
  return pc->traverseTree( extent, n, maxDepth );
}

bool QgsPointCloudLayerRenderer::render()
{
  // TODO cache!?
  QgsPointCloudIndex *pc = mLayer->dataProvider()->index();

  QgsRenderContext &context = *renderContext();

  // Set up the render configuration options
  QPainter *painter = context.painter();

  painter->save();
  context.setPainterFlagsUsingContext( painter );

  QgsPointCloudDataBounds db;

  QElapsedTimer t;
  t.start();

  // TODO: set depth based on map units per pixel
  int depth = 3;
  QList<IndexedPointCloudNode> nodes = _traverseTree( pc, context.mapExtent(), pc->root(), depth );

  // drawing
  for ( auto n : nodes )
  {
    if ( context.renderingStopped() )
    {
      qDebug() << "canceled";
      break;
    }
    drawData( painter, pc->nodePositionDataAsInt32( n ), mConfig );
  }

  qDebug() << "totals:" << nodesDrawn << "nodes | " << pointsDrawn << " points | " << t.elapsed() << "ms";

  painter->restore();

  return true;
}


QgsPointCloudLayerRenderer::~QgsPointCloudLayerRenderer() = default;

void QgsPointCloudLayerRenderer::drawData( QPainter *painter, const QVector<qint32> &data, const QgsPointCloudRendererConfig &config )
{
  const QgsMapToPixel mapToPixel = renderContext()->mapToPixel();
  const QgsVector3D scale = mLayer->dataProvider()->index()->scale();
  const QgsVector3D offset = mLayer->dataProvider()->index()->offset();

  QgsRectangle mapExtent = renderContext()->mapExtent();

  QPen pen;
  pen.setWidth( config.penWidth() );
  pen.setCapStyle( Qt::FlatCap );
  //pen.setJoinStyle( Qt::MiterJoin );

  const qint32 *ptr = data.constData();
  int count = data.count() / 3;
  for ( int i = 0; i < count; ++i )
  {
    qint32 ix = ptr[i * 3 + 0];
    qint32 iy = ptr[i * 3 + 1];
    qint32 iz = ptr[i * 3 + 2];

    double x = offset.x() + scale.x() * ix;
    double y = offset.y() + scale.y() * iy;
    if ( mapExtent.contains( QgsPointXY( x, y ) ) )
    {
      double z = offset.z() + scale.z() * iz;
      mapToPixel.transformInPlace( x, y );

      pen.setColor( config.colorRamp()->color( ( z - config.zMin() ) / ( config.zMax() - config.zMin() ) ) );
      painter->setPen( pen );
      painter->drawPoint( QPointF( x, y ) );
    }
  }

  // stats
  ++nodesDrawn;
  pointsDrawn += count;
}
