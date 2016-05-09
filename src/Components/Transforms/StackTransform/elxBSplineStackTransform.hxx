/*=========================================================================
 *
 *  Copyright UMC Utrecht and contributors
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/
#ifndef __elxBSplineStackTransform_hxx
#define __elxBSplineStackTransform_hxx

#include "elxBSplineStackTransform.h"

#include "itkImageRegionExclusionConstIteratorWithIndex.h"
#include "vnl/vnl_math.h"

namespace elastix
{

/**
 * ********************* Constructor ****************************
 */

template< class TElastix >
BSplineStackTransform< TElastix >
::BSplineStackTransform()
{} // end Constructor()

/**
 * ************ InitializeBSplineTransform ***************
 */
template< class TElastix >
unsigned int
BSplineStackTransform< TElastix >
::InitializeBSplineTransform()
{
  /** Initialize the right BSplineTransform and GridScheduleComputer. */
  this->m_GridScheduleComputer = GridScheduleComputerType::New();
  this->m_GridScheduleComputer->SetBSplineOrder( m_SplineOrder );
  if( m_SplineOrder == 1 )
  {
    this->m_BSplineDummySubTransform = BSplineTransformLinearType::New();
  }
  else if( m_SplineOrder == 2 )
  {
    this->m_BSplineDummySubTransform = BSplineTransformQuadraticType::New();
  }
  else if( m_SplineOrder == 3 )
  {
    this->m_BSplineDummySubTransform = BSplineTransformCubicType::New();
  }
  else
  {
    itkExceptionMacro( << "ERROR: The provided spline order is not supported." );
    return 1;
  }

  /** Note: periodic B-splines are not supported here as they do not seem to
   * make sense as a subtransform and deliver problems when compiling elastix
   * for image dimension 2.
   */

  /** Create stack transform. */
  this->m_BSplineStackTransform = BSplineStackTransformType::New();

  /** Set stack transform as current transform. */
  this->SetCurrentTransform( this->m_BSplineStackTransform );

  /** Create grid upsampler. */
  this->m_GridUpsampler = GridUpsamplerType::New();
  this->m_GridUpsampler->SetBSplineOrder( m_SplineOrder );

  return 0;
}


/**
 * ******************* BeforeAll ***********************
 */

template< class TElastix >
int
BSplineStackTransform< TElastix >
::BeforeAll( void )
{
  /** Read spline order from configuration file. */
  m_SplineOrder = 3;
  this->GetConfiguration()->ReadParameter( m_SplineOrder,
    "BSplineTransformSplineOrder", this->GetComponentLabel(), 0, 0, true );

  /** Initialize B-spline transform and grid scheduler. */
  return InitializeBSplineTransform();
}


/**
 * ******************* BeforeRegistration ***********************
 */

template< class TElastix >
void
BSplineStackTransform< TElastix >
::BeforeRegistration( void )
{
  /** Set initial transform parameters to a 1x1x1 grid, with deformation (0,0,0).
   * In the method BeforeEachResolution() this will be replaced by the right grid size.
   * This seems not logical, but it is required, since the registration
   * class checks if the number of parameters in the transform is equal to
   * the number of parameters in the registration class. This check is done
   * before calling the BeforeEachResolution() methods.
   */

  /** Task 1 - Set the Grid. */

  /** Declarations. */
  ReducedDimensionRegionType  gridregion;
  ReducedDimensionSizeType    gridsize;
  ReducedDimensionIndexType   gridindex;
  ReducedDimensionSpacingType gridspacing;
  ReducedDimensionOriginType  gridorigin;

  /** Fill everything with default values. */
  gridsize.Fill( 1 );
  gridindex.Fill( 0 );
  gridspacing.Fill( 1.0 );
  gridorigin.Fill( 0.0 );

  /** Set gridsize for large dimension to 4 to prevent errors when checking
   * on support region size.
   */
  gridsize.SetElement( gridsize.GetSizeDimension() - 1, 4 );

  /** Set it all. */
  gridregion.SetIndex( gridindex );
  gridregion.SetSize( gridsize );
  this->m_BSplineDummySubTransform->SetGridRegion( gridregion );
  this->m_BSplineDummySubTransform->SetGridSpacing( gridspacing );
  this->m_BSplineDummySubTransform->SetGridOrigin( gridorigin );

  /** Task 2 - Set the stack transform parameters. */

  /** Determine stack transform settings. Here they are based on the fixed image. */
  const SizeType imageSize = this->GetElastix()->GetFixedImage()->GetLargestPossibleRegion().GetSize();
  this->m_NumberOfSubTransforms = imageSize[ SpaceDimension - 1 ];
  this->m_StackSpacing          = this->GetElastix()->GetFixedImage()->GetSpacing()[ SpaceDimension - 1 ];
  this->m_StackOrigin           = this->GetElastix()->GetFixedImage()->GetOrigin()[ SpaceDimension - 1 ];

  /** Set stack transform parameters. */
  this->m_BSplineStackTransform->SetNumberOfSubTransforms( this->m_NumberOfSubTransforms );
  this->m_BSplineStackTransform->SetStackOrigin( this->m_StackOrigin );
  this->m_BSplineStackTransform->SetStackSpacing( this->m_StackSpacing );

  /** Initialize stack sub transforms. */
  this->m_BSplineStackTransform->SetAllSubTransforms( this->m_BSplineDummySubTransform );

  /** Task 3 - Give the registration an initial parameter-array. */
  ParametersType dummyInitialParameters( this->GetNumberOfParameters() );
  dummyInitialParameters.Fill( 0.0 );

  /** Put parameters in the registration. */
  this->m_Registration->GetAsITKBaseType()
  ->SetInitialTransformParameters( dummyInitialParameters );

  /** Precompute the B-spline grid regions. */
  this->PreComputeGridInformation();

} // end BeforeRegistration()


/**
 * ***************** BeforeEachResolution ***********************
 */

template< class TElastix >
void
BSplineStackTransform< TElastix >
::BeforeEachResolution( void )
{
  /** What is the current resolution level? */
  unsigned int level
    = this->m_Registration->GetAsITKBaseType()->GetCurrentLevel();

  /** Define the grid. */
  if( level == 0 )
  {
    this->InitializeTransform();
  }
  else
  {
    /** Upsample the B-spline grid for all sub transforms, if required. */
    this->IncreaseScale();
  }

  /** Get the PassiveEdgeWidth and use it to set the OptimizerScales. */
  unsigned int passiveEdgeWidth = 0;
  this->GetConfiguration()->ReadParameter( passiveEdgeWidth,
    "PassiveEdgeWidth", this->GetComponentLabel(), level, 0, false );
  this->SetOptimizerScales( passiveEdgeWidth );

} // end BeforeEachResolution()


/**
 * ******************** PreComputeGridInformation ***********************
 */

template< class TElastix >
void
BSplineStackTransform< TElastix >
::PreComputeGridInformation( void )
{
  /** Get the total number of resolution levels. */
  const unsigned int nrOfResolutions
    = this->m_Registration->GetAsITKBaseType()->GetNumberOfLevels();

  /** Get current image origin, spacing, direction and largest possible region. */
  const OriginType    origin    = this->GetElastix()->GetFixedImage()->GetOrigin();
  const SpacingType   spacing   = this->GetElastix()->GetFixedImage()->GetSpacing();
  const DirectionType direction = this->GetElastix()->GetFixedImage()->GetDirection();
  const RegionType    region    = this->GetElastix()->GetFixedImage()->GetLargestPossibleRegion();

  /** Variables to store reduced dimension origin, spacing, direction and region in. */
  ReducedDimensionOriginType    rorigin;
  ReducedDimensionSpacingType   rspacing;
  ReducedDimensionDirectionType rdirection;
  ReducedDimensionRegionType    rregion;

  /** Reduce dimension of origin, spacing, direction and region. */
  for( unsigned int d = 0; d < ReducedSpaceDimension; ++d )
  {
    rorigin[ d ]  = origin[ d ];
    rspacing[ d ] = spacing[ d ];
    rregion.SetSize( d, region.GetSize( d ) );
    rregion.SetIndex( d, region.GetIndex( d ) );
    for( unsigned int e = 0; e < ReducedSpaceDimension; ++e )
    {
      rdirection[ d ][ e ] = direction[ d ][ e ];
    }
  }

  /** Set up grid schedule computer with reduced dimension image info. */
  this->m_GridScheduleComputer->SetImageOrigin( rorigin );
  this->m_GridScheduleComputer->SetImageSpacing( rspacing );
  this->m_GridScheduleComputer->SetImageDirection( rdirection );
  this->m_GridScheduleComputer->SetImageRegion( rregion );

  /** Take the initial transform only into account, if composition is used. */
  if( this->GetUseComposition() )
  {
    /** \todo To do this, we need a grid schedule computer which can handle an
     * initial transform of a higher dimension than the grid. We probably need
     * to program some kind of stack grid schedule computer for that.
     */
    // this->m_GridScheduleComputer->SetInitialTransform( this->Superclass1::GetInitialTransform() );
  }

  /** Get the grid spacing schedule from the parameter file.
   *
   * Method 1: The user specifies "FinalGridSpacingInVoxels"
   * Method 2: The user specifies "FinalGridSpacingInPhysicalUnits"
   *
   * Method 1 and 2 additionally take the "GridSpacingSchedule".
   * The GridSpacingSchedule is defined by downsampling factors
   * for each resolution, for each dimension (just like the image
   * pyramid schedules). So, for 2D images, and 3 resulutions,
   * we can specify:
   * (GridSpacingSchedule 4.0 4.0 2.0 2.0 1.0 1.0)
   * Which is the default schedule, if no GridSpacingSchedule is supplied.
   */

  /** Determine which method is used. */
  bool        method1 = false;
  std::size_t count1  = this->m_Configuration
    ->CountNumberOfParameterEntries( "FinalGridSpacingInVoxels" );
  if( count1 > 0 )
  {
    method1 = true;
  }

  bool        method2 = false;
  std::size_t count2  = this->m_Configuration
    ->CountNumberOfParameterEntries( "FinalGridSpacingInPhysicalUnits" );
  if( count2 > 0 )
  {
    method2 = true;
  }

  /** Throw an exception if both methods are used. */
  if( count1 > 0 && count2 > 0 )
  {
    itkExceptionMacro( << "ERROR: You can not specify both \"FinalGridSpacingInVoxels\""
        " and \"FinalGridSpacingInPhysicalUnits\" in the parameter file." );
  }

  /** Declare variables and set defaults. */
  ReducedDimensionSpacingType finalGridSpacingInVoxels;
  ReducedDimensionSpacingType finalGridSpacingInPhysicalUnits;
  finalGridSpacingInVoxels.Fill( 16.0 );
  finalGridSpacingInPhysicalUnits.Fill( 8.0 );

  /** Method 1: Read the FinalGridSpacingInVoxels. */
  if( method1 )
  {
    for( unsigned int dim = 0; dim < ReducedSpaceDimension; ++dim )
    {
      this->m_Configuration->ReadParameter(
        finalGridSpacingInVoxels[ dim ], "FinalGridSpacingInVoxels",
        this->GetComponentLabel(), dim, 0 );
    }

    /** Compute the grid spacing in physical units. */
    for( unsigned int dim = 0; dim < ReducedSpaceDimension; ++dim )
    {
      finalGridSpacingInPhysicalUnits[ dim ]
        = finalGridSpacingInVoxels[ dim ]
        * this->GetElastix()->GetFixedImage()->GetSpacing()[ dim ];
    }
  }

  /** Method 2: Read the FinalGridSpacingInPhysicalUnits. */
  if( method2 )
  {
    for( unsigned int dim = 0; dim < ReducedSpaceDimension; ++dim )
    {
      this->m_Configuration->ReadParameter(
        finalGridSpacingInPhysicalUnits[ dim ], "FinalGridSpacingInPhysicalUnits",
        this->GetComponentLabel(), dim, 0 );
    }
  }

  /** Set up a default grid spacing schedule. */
  this->m_GridScheduleComputer->SetDefaultSchedule(
    nrOfResolutions, 2.0 );
  GridScheduleType gridSchedule;
  this->m_GridScheduleComputer->GetSchedule( gridSchedule );

  /** Read what the user has specified. This overrules everything. */
  count2 = this->m_Configuration
    ->CountNumberOfParameterEntries( "GridSpacingSchedule" );
  unsigned int entry_nr = 0;
  if( count2 == 0 )
  {
    // keep the default schedule
  }
  else if( count2 == nrOfResolutions )
  {
    for( unsigned int res = 0; res < nrOfResolutions; ++res )
    {
      for( unsigned int dim = 0; dim < ReducedSpaceDimension; ++dim )
      {
        this->m_Configuration->ReadParameter( gridSchedule[ res ][ dim ],
          "GridSpacingSchedule", entry_nr, false );
      }
      ++entry_nr;
    }
  }
  else if( count2 == nrOfResolutions * ReducedSpaceDimension )
  {
    for( unsigned int res = 0; res < nrOfResolutions; ++res )
    {
      for( unsigned int dim = 0; dim < ReducedSpaceDimension; ++dim )
      {
        this->m_Configuration->ReadParameter( gridSchedule[ res ][ dim ],
          "GridSpacingSchedule", entry_nr, false );
        ++entry_nr;
      }
    }
  }
  else
  {
    xl::xout[ "error" ]
      << "ERROR: Invalid GridSpacingSchedule! The number of entries"
      << " behind the GridSpacingSchedule option should equal the"
      << " numberOfResolutions, or the numberOfResolutions * ( ImageDimension - 1 )."
      << std::endl;
    itkExceptionMacro( << "ERROR: Invalid GridSpacingSchedule!" );
  }

  /** Set the grid schedule and final grid spacing in the schedule computer. */
  this->m_GridScheduleComputer->SetFinalGridSpacing(
    finalGridSpacingInPhysicalUnits );
  this->m_GridScheduleComputer->SetSchedule( gridSchedule );

  /** Compute the necessary information. */
  this->m_GridScheduleComputer->ComputeBSplineGrid();

} // end PreComputeGridInformation()


/**
 * ******************** InitializeTransform ***********************
 *
 * Set the size of the initial control point grid and initialize
 * the parameters to 0.
 */

template< class TElastix >
void
BSplineStackTransform< TElastix >
::InitializeTransform( void )
{
  /** Compute the B-spline grid region, origin, and spacing. */
  ReducedDimensionRegionType    gridRegion;
  ReducedDimensionOriginType    gridOrigin;
  ReducedDimensionSpacingType   gridSpacing;
  ReducedDimensionDirectionType gridDirection;
  this->m_GridScheduleComputer->GetBSplineGrid( 0,
    gridRegion, gridSpacing, gridOrigin, gridDirection );

  /** Set it in the BSplineTransform. */
  this->m_BSplineDummySubTransform->SetGridRegion( gridRegion );
  this->m_BSplineDummySubTransform->SetGridSpacing( gridSpacing );
  this->m_BSplineDummySubTransform->SetGridOrigin( gridOrigin );
  this->m_BSplineDummySubTransform->SetGridDirection( gridDirection );

  /** Set all subtransforms to a copy of the dummy B-spline sub transform. */
  this->m_BSplineStackTransform->SetAllSubTransforms( this->m_BSplineDummySubTransform );

  /** Set initial parameters for the first resolution to 0.0. */
  ParametersType initialParameters( this->GetNumberOfParameters() );
  initialParameters.Fill( 0.0 );
  this->m_Registration->GetAsITKBaseType()
  ->SetInitialTransformParametersOfNextLevel( initialParameters );

} // end InitializeTransform()


/**
 * *********************** IncreaseScale ************************
 *
 * Upsample the grid of control points.
 */

template< class TElastix >
void
BSplineStackTransform< TElastix >
::IncreaseScale( void )
{
  /** What is the current resolution level? */
  unsigned int level
    = this->m_Registration->GetAsITKBaseType()->GetCurrentLevel();

  /** Get first sub transform. */
  ReducedDimensionBSplineTransformBasePointer firstsubtransform
    = dynamic_cast< ReducedDimensionBSplineTransformBaseType * >(
    this->m_BSplineStackTransform->GetSubTransform( 0 ).GetPointer() );

  /** Get the current grid settings. */
  ReducedDimensionOriginType    currentGridOrigin    = firstsubtransform->GetGridOrigin();
  ReducedDimensionSpacingType   currentGridSpacing   = firstsubtransform->GetGridSpacing();
  ReducedDimensionRegionType    currentGridRegion    = firstsubtransform->GetGridRegion();
  ReducedDimensionDirectionType currentGridDirection = firstsubtransform->GetGridDirection();

  /** Determine new required grid settings. */
  ReducedDimensionOriginType    requiredGridOrigin;
  ReducedDimensionSpacingType   requiredGridSpacing;
  ReducedDimensionRegionType    requiredGridRegion;
  ReducedDimensionDirectionType requiredGridDirection;
  this->m_GridScheduleComputer->GetBSplineGrid( level,
    requiredGridRegion, requiredGridSpacing, requiredGridOrigin, requiredGridDirection );

  /** Setup the GridUpsampler. */
  this->m_GridUpsampler->SetCurrentGridOrigin( currentGridOrigin );
  this->m_GridUpsampler->SetCurrentGridSpacing( currentGridSpacing );
  this->m_GridUpsampler->SetCurrentGridRegion( currentGridRegion );
  this->m_GridUpsampler->SetCurrentGridDirection( currentGridDirection );
  this->m_GridUpsampler->SetRequiredGridOrigin( requiredGridOrigin );
  this->m_GridUpsampler->SetRequiredGridSpacing( requiredGridSpacing );
  this->m_GridUpsampler->SetRequiredGridRegion( requiredGridRegion );
  this->m_GridUpsampler->SetRequiredGridDirection( requiredGridDirection );

  for( unsigned int t = 0; t < this->m_NumberOfSubTransforms; ++t )
  {
    /** Get sub transform pointer. */
    ReducedDimensionBSplineTransformBasePointer subtransform
      = dynamic_cast< ReducedDimensionBSplineTransformBaseType * >( this->m_BSplineStackTransform->GetSubTransform( t ).GetPointer() );

    /** Get the lastest subtransform parameters. */
    ParametersType latestParameters
      = subtransform->GetParameters();

    /** Compute the upsampled B-spline parameters. */
    ParametersType upsampledParameters;
    this->m_GridUpsampler->UpsampleParameters( latestParameters, upsampledParameters );

    /** Set the new grid definition in the BSplineTransform. */
    subtransform->SetGridOrigin( requiredGridOrigin );
    subtransform->SetGridSpacing( requiredGridSpacing );
    subtransform->SetGridRegion( requiredGridRegion );
    subtransform->SetGridDirection( requiredGridDirection );

    /** Set the initial parameters for the next level. */
    subtransform->SetParametersByValue( upsampledParameters );
  }

  /** Set the initial parameters for the next level. */
  this->m_Registration->GetAsITKBaseType()
  ->SetInitialTransformParametersOfNextLevel( this->GetParameters() );

}  // end IncreaseScale()


/**
 * ************************* ReadFromFile ************************
 */

template< class TElastix >
void
BSplineStackTransform< TElastix >
::ReadFromFile( void )
{

  /** Read spline order settings and initialize BSplineTransform. */
  m_SplineOrder = 3;
  this->GetConfiguration()->ReadParameter( m_SplineOrder,
    "BSplineTransformSplineOrder", this->GetComponentLabel(), 0, 0 );

  /** Read stack-spacing, stack-origin and number of sub-transforms. */
  bool dummy = this->GetConfiguration()->ReadParameter( this->m_NumberOfSubTransforms,
    "NumberOfSubTransforms", this->GetComponentLabel(), 0, 0 );
  dummy |= this->GetConfiguration()->ReadParameter( this->m_StackOrigin,
    "StackOrigin", this->GetComponentLabel(), 0, 0 );
  dummy |= this->GetConfiguration()->ReadParameter( this->m_StackSpacing,
    "StackSpacing", this->GetComponentLabel(), 0, 0 );

  /** Initialize the right B-spline transform. */
  InitializeBSplineTransform();

  /** Set stack transform parameters. */
  this->m_BSplineStackTransform->SetNumberOfSubTransforms( this->m_NumberOfSubTransforms );
  this->m_BSplineStackTransform->SetStackOrigin( this->m_StackOrigin );
  this->m_BSplineStackTransform->SetStackSpacing( this->m_StackSpacing );

  /** Read and Set the Grid. */

  /** Declarations. */
  ReducedDimensionRegionType    gridregion;
  ReducedDimensionSizeType      gridsize;
  ReducedDimensionIndexType     gridindex;
  ReducedDimensionSpacingType   gridspacing;
  ReducedDimensionOriginType    gridorigin;
  ReducedDimensionDirectionType griddirection;

  /** Fill everything with default values. */
  gridsize.Fill( 1 );
  gridindex.Fill( 0 );
  gridspacing.Fill( 1.0 );
  gridorigin.Fill( 0.0 );
  griddirection.SetIdentity();

  /** Get GridSize, GridIndex, GridSpacing and GridOrigin. */
  for( unsigned int i = 0; i < ReducedSpaceDimension; i++ )
  {
    dummy |= this->m_Configuration->ReadParameter( gridsize[ i ], "GridSize", i );
    dummy |= this->m_Configuration->ReadParameter( gridindex[ i ], "GridIndex", i );
    dummy |= this->m_Configuration->ReadParameter( gridspacing[ i ], "GridSpacing", i );
    dummy |= this->m_Configuration->ReadParameter( gridorigin[ i ], "GridOrigin", i );
    for( unsigned int j = 0; j < ReducedSpaceDimension; j++ )
    {
      this->m_Configuration->ReadParameter( griddirection( j, i ),
        "GridDirection", i * ReducedSpaceDimension + j );
    }
  }

  if( !dummy )
  {
    itkExceptionMacro( "NumberOfSubTransforms, StackOrigin, StackSpacing, GridSize, "
                    << "GridIndex, GridSpacing and GridOrigin is required by "
                    << this->GetNameOfClass() << "." )
  }

  /** Set it all. */
  gridregion.SetIndex( gridindex );
  gridregion.SetSize( gridsize );
  this->m_BSplineDummySubTransform->SetGridRegion( gridregion );
  this->m_BSplineDummySubTransform->SetGridSpacing( gridspacing );
  this->m_BSplineDummySubTransform->SetGridOrigin( gridorigin );
  this->m_BSplineDummySubTransform->SetGridDirection( griddirection );

  /** Set stack subtransforms. */
  this->m_BSplineStackTransform->SetAllSubTransforms( this->m_BSplineDummySubTransform );

  /** Call the ReadFromFile from the TransformBase.
   * This must be done after setting the Grid, because later the
   * ReadFromFile from TransformBase calls SetParameters, which
   * checks the parameter-size, which is based on the GridSize.
   */
  this->Superclass2::ReadFromFile();

} // end ReadFromFile()


/**
 * ************************* WriteToFile ************************
 *
 * Saves the TransformParameters as a vector and if wanted
 * also as a deformation field.
 */

template< class TElastix >
void
BSplineStackTransform< TElastix >
::WriteToFile( const ParametersType & param ) const
{
  /** Call the WriteToFile from the TransformBase. */
  this->Superclass2::WriteToFile( param );

  /** Add some BSplineTransform specific lines. */
  xout[ "transpar" ] << std::endl << "// BSplineStackTransform specific" << std::endl;

  /** Get the GridSize, GridIndex, GridSpacing,
   * GridOrigin, and GridDirection of this transform. */
  ReducedDimensionBSplineTransformBasePointer firstSubTransform
    = dynamic_cast< ReducedDimensionBSplineTransformBaseType * >( this->m_BSplineStackTransform->GetSubTransform( 0 ).GetPointer() );
  ReducedDimensionSizeType      size      = firstSubTransform->GetGridRegion().GetSize();
  ReducedDimensionIndexType     index     = firstSubTransform->GetGridRegion().GetIndex();
  ReducedDimensionSpacingType   spacing   = firstSubTransform->GetGridSpacing();
  ReducedDimensionOriginType    origin    = firstSubTransform->GetGridOrigin();
  ReducedDimensionDirectionType direction = firstSubTransform->GetGridDirection();

  /** Write the GridSize of this transform. */
  xout[ "transpar" ] << "(GridSize ";
  for( unsigned int i = 0; i < ReducedSpaceDimension; i++ )
  {
    xout[ "transpar" ] << size[ i ] << " ";
  }
  xout[ "transpar" ] << ")" << std::endl;

  /** Write the GridIndex of this transform. */
  xout[ "transpar" ] << "(GridIndex ";
  for( unsigned int i = 0; i < ReducedSpaceDimension; i++ )
  {
    xout[ "transpar" ] << index[ i ] << " ";
  }
  xout[ "transpar" ] << ")" << std::endl;

  /** Set the precision of cout to 2, because GridSpacing and
   * GridOrigin must have at least one digit precision.
   */
  xout[ "transpar" ] << std::setprecision( 10 );

  /** Write the GridSpacing of this transform. */
  xout[ "transpar" ] << "(GridSpacing ";
  for( unsigned int i = 0; i < ReducedSpaceDimension; i++ )
  {
    xout[ "transpar" ] << spacing[ i ] << " ";
  }
  xout[ "transpar" ] << ")" << std::endl;

  /** Write the GridOrigin of this transform. */
  xout[ "transpar" ] << "(GridOrigin ";
  for( unsigned int i = 0; i < ReducedSpaceDimension; i++ )
  {
    xout[ "transpar" ] << origin[ i ] << " ";
  }
  xout[ "transpar" ] << ")" << std::endl;

  /** Write the GridDirection of this transform. */
  xout[ "transpar" ] << "(GridDirection";
  for( unsigned int i = 0; i < ReducedSpaceDimension; i++ )
  {
    for( unsigned int j = 0; j < ReducedSpaceDimension; j++ )
    {
      xout[ "transpar" ] << " " << direction( j, i );
    }
  }
  xout[ "transpar" ] << ")" << std::endl;

  /** Write the spline order of this transform. */
  xout[ "transpar" ] << "(BSplineTransformSplineOrder " << m_SplineOrder << ")" << std::endl;

  /** Write the stack spacing, stack origina and number of sub transforms. */
  xout[ "transpar" ] << "(StackSpacing " << this->m_BSplineStackTransform->GetStackSpacing() << ")" << std::endl;
  xout[ "transpar" ] << "(StackOrigin " << this->m_BSplineStackTransform->GetStackOrigin() << ")" << std::endl;
  xout[ "transpar" ] << "(NumberOfSubTransforms " << this->m_BSplineStackTransform->GetNumberOfSubTransforms() << ")" << std::endl;

  /** Set the precision back to default value. */
  xout[ "transpar" ] << std::setprecision(
    this->m_Elastix->GetDefaultOutputPrecision() );

} // end WriteToFile()


/**
 * *********************** SetOptimizerScales ***********************
 *
 * Set the optimizer scales of the edge coefficients to infinity.
 */

template< class TElastix >
void
BSplineStackTransform< TElastix >::SetOptimizerScales( const unsigned int edgeWidth )
{
  /** Some typedefs. */
  typedef itk::ImageRegionExclusionConstIteratorWithIndex< ImageType > IteratorType;
  typedef typename RegistrationType::ITKBaseType                       ITKRegistrationType;
  typedef typename ITKRegistrationType::OptimizerType                  OptimizerType;
  typedef typename OptimizerType::ScalesType                           ScalesType;
  typedef typename ScalesType::ValueType                               ScalesValueType;

  /** Define new scales. */
  const NumberOfParametersType numberOfParameters
    = this->m_BSplineDummySubTransform->GetNumberOfParameters();
  const unsigned long offset = numberOfParameters / SpaceDimension;
  ScalesType          newScales( numberOfParameters );
  newScales.Fill( itk::NumericTraits< ScalesValueType >::OneValue() );
  const ScalesValueType infScale = 10000.0;

  if( edgeWidth == 0 )
  {
    /** Just set the unit scales into the optimizer. */
    this->m_Registration->GetAsITKBaseType()->GetOptimizer()->SetScales( newScales );
    return;
  }

  /** Get the grid region information and create a fake coefficient image. */
  BSplineTransformBasePointer firstSubTransform
    = dynamic_cast< BSplineTransformBaseType * >( this->m_BSplineStackTransform->GetSubTransform( 0 ).GetPointer() );
  RegionType   gridregion = firstSubTransform->GetGridRegion();
  SizeType     gridsize   = gridregion.GetSize();
  IndexType    gridindex  = gridregion.GetIndex();
  ImagePointer coeff      = ImageType::New();
  coeff->SetRegions( gridregion );
  coeff->Allocate();

  /** Determine inset region. (so, the region with active parameters). */
  RegionType insetgridregion;
  SizeType   insetgridsize;
  IndexType  insetgridindex;
  for( unsigned int i = 0; i < SpaceDimension; ++i )
  {
    insetgridsize[ i ] = static_cast< unsigned int >( vnl_math_max(
      0, static_cast< int >( gridsize[ i ] - 2 * edgeWidth ) ) );
    if( insetgridsize[ i ] == 0 )
    {
      xl::xout[ "error" ]
        << "ERROR: you specified a PassiveEdgeWidth of "
        << edgeWidth
        << ", while the total grid size in dimension "
        << i
        << " is only "
        << gridsize[ i ] << "." << std::endl;
      itkExceptionMacro( << "ERROR: the PassiveEdgeWidth is too large!" );
    }
    insetgridindex[ i ] = gridindex[ i ] + edgeWidth;
  }
  insetgridregion.SetSize( insetgridsize );
  insetgridregion.SetIndex( insetgridindex );

  /** Set up iterator over the coefficient image. */
  IteratorType cIt( coeff, coeff->GetLargestPossibleRegion() );
  cIt.SetExclusionRegion( insetgridregion );
  cIt.GoToBegin();

  /** Set the scales to infinity that correspond to edge coefficients
   * This (hopefully) makes sure they are not optimized during registration.
   */
  while( !cIt.IsAtEnd() )
  {
    const IndexType &   index      = cIt.GetIndex();
    const unsigned long baseOffset = coeff->ComputeOffset( index );
    for( unsigned int i = 0; i < SpaceDimension; ++i )
    {
      const unsigned int scalesIndex = static_cast< unsigned int >(
        baseOffset + i * offset );
      newScales[ scalesIndex ] = infScale;
    }
    ++cIt;
  }

  /** Set the scales into the optimizer. */
  this->m_Registration->GetAsITKBaseType()->GetOptimizer()->SetScales( newScales );

} // end SetOptimizerScales()

/**
 * ************************* CreateTransformParametersMap ************************
 *
 * Creates the TransformParametersmap
 *
 */

template< class TElastix >
void
BSplineStackTransform< TElastix >
::CreateTransformParametersMap(
  const ParametersType & param,
  ParameterMapType * paramsMap ) const
{
  char tmpValue[ 256 ];

  /** Call the CreateTransformParametersMap from the TransformBase. */
  this->Superclass2::CreateTransformParametersMap( param, paramsMap );

  /** Write BSplineStackTransform-specific parameters */

  /** Get the GridSize, GridIndex, GridSpacing,
   * GridOrigin, and GridDirection of this transform. */
  ReducedDimensionBSplineTransformBasePointer firstSubTransform
    = dynamic_cast< ReducedDimensionBSplineTransformBaseType * >( this->m_BSplineStackTransform->GetSubTransform( 0 ).GetPointer() );
  ReducedDimensionSizeType      size      = firstSubTransform->GetGridRegion().GetSize();
  ReducedDimensionIndexType     index     = firstSubTransform->GetGridRegion().GetIndex();
  ReducedDimensionSpacingType   spacing   = firstSubTransform->GetGridSpacing();
  ReducedDimensionOriginType    origin    = firstSubTransform->GetGridOrigin();
  ReducedDimensionDirectionType direction = firstSubTransform->GetGridDirection();

  /** Write the GridSize of this transform. */
  ParameterValueType GridSize;
  for( unsigned int i = 0; i < ReducedSpaceDimension; i++ )
  {
    sprintf( tmpValue, "%.10lu", size[ i ] );
    GridSize.push_back( tmpValue );
  }
  paramsMap->insert( make_pair( "GridSize", GridSize ) );

  /** Write the GridIndex of this transform. */
  ParameterValueType GridIndex;
  for( unsigned int i = 0; i < ReducedSpaceDimension; i++ )
  {
    sprintf( tmpValue, "%.10ld", index[ i ] );
    GridIndex.push_back( tmpValue );
  }
  paramsMap->insert( make_pair( "GridIndex", GridIndex ) );

  /** Write the GridSpacing of this transform.  */
  std::vector< std::string > GridSpacing;
  for( unsigned int i = 0; i < ReducedSpaceDimension; i++ )
  {
    sprintf( tmpValue, "%.10lf", spacing[ i ] );
    GridSpacing.push_back( tmpValue );
  }
  paramsMap->insert( make_pair( "GridSpacing", GridSpacing ) );

  /** Write the GridOrigin of this transform. */
  ParameterValueType GridOrigin;
  for( unsigned int i = 0; i < ReducedSpaceDimension; i++ )
  {
    sprintf( tmpValue, "%.10lf", origin[ i ] );
    GridOrigin.push_back( tmpValue );
  }
  paramsMap->insert( make_pair( "GridOrigin", GridOrigin ) );

  /** Write the GridDirection of this transform. */
  ParameterValueType GridDirection;
  for( unsigned int i = 0; i < ReducedSpaceDimension; i++ )
  {
    for( unsigned int j = 0; j < ReducedSpaceDimension; j++ )
    {
      sprintf( tmpValue, "%.10lf", direction( j, i ) );
      GridDirection.push_back( tmpValue );
    }
  }
  paramsMap->insert( make_pair( "GridDirection", GridDirection ) );

  /** Write the spline order of this transform. */
  sprintf( tmpValue, "%i", m_SplineOrder );
  paramsMap->insert( make_pair( "BSplineTransformSplineOrder", ParameterValueType( 1, tmpValue ) ) );

  /** Write the stack spacing, stack origin and number of sub transforms. */
  sprintf( tmpValue, "%.10lf", this->m_BSplineStackTransform->GetStackSpacing() );
  paramsMap->insert( make_pair( "StackSpacing", ParameterValueType( 1, tmpValue ) ) );
  sprintf( tmpValue, "%.10lf", this->m_BSplineStackTransform->GetStackOrigin() );
  paramsMap->insert( make_pair( "StackOrigin", ParameterValueType( 1, tmpValue ) ) );
  sprintf( tmpValue, "%i", this->m_BSplineStackTransform->GetNumberOfSubTransforms() );
  paramsMap->insert( make_pair( "NumberOfSubTransforms", ParameterValueType( 1, tmpValue ) ) );
}

} // end namespace elastix

#endif // end #ifndef __elxBSplineStackTransform_hxx
