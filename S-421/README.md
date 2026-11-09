https://cirm.org/s-421

## GML(IEC-63173-1-ED2 12.2)

Ready to use test material is available for:

- minimum generic test (RTE-TEST-GMIN.gml): Minimum generic test data includes only the data required for mandatory objects, specifically Route and RouteInfo, as defined by the schema, including all related mandatory attributes. 
- basic generic test (RTE-TEST-GBASIC.gml): Basic generic test data includes only the mandatory attributes for all objects in route plan. Each route plan within the test data contains at least 10 waypoints from shore to ship.
- full generic test (RTE-TEST-GFULL.gml): Full generic test data includes all optional objects and attributes as well as mandatory objects and attributes defined by the applicable schema. 

Additional test files are required to test schema validation and to provide more examples for 
test execution. Such test files can be created by editing the generic test files, for example: 

- optional attributes can be removed from the generic test material; 
- value of attribute can be changed to be appropriate for the test
- incomplete route plan can be created by removing mandatory attributes
- incomplete route plan can be created by setting a routeInfoValidityEnd before routeInfoValidityStart, or ETD before ETA on a routeScheduleElement
- invalid route plan: schema validation error can be created by duplicating an attribute
- invalid route plan: application level context check error can be created by changing the number of decimals (for example as less or more than allowed range)
- invalid route plan: application level context check error can be created by changing the geometry for meridional difference of over 180°
- invalid route plan: schema validation error can be created by removing field start or end markings of the XML encoding
- invalid route plan: schema validation error can be created by changing the value of an attribute as not compliant with attribute specification (for example: value out of legal range, different data type, such as characters instead of numeric value)
- invalid route plan: schema validation error can be created by removing a required field.

## GML ID

IEC-63173-1-ED2 Annex B Table B.1 - Example of GML ID